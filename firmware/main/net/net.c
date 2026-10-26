#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "model.h"
#include "sdkconfig.h"
#include "ui.h"

static const char *TAG = "net";

static esp_websocket_client_handle_t s_ws;
static QueueHandle_t s_outbox;
static esp_timer_handle_t s_wifi_retry;
static uint32_t s_wifi_backoff_ms = 1000;
static bool s_ws_up;
static bool s_started_services;

static char *s_rx;      // reassembly buffer for a fragmented snapshot
static size_t s_rx_cap;
static dk_model_t *s_model;

// ------------------------------------------------------------- UI glue

static void set_conn(dk_conn_t c, const char *detail)
{
    if (lvgl_port_lock(1000)) {
        ui_set_connection(c, detail);
        lvgl_port_unlock();
    }
}

static void deliver_snapshot(const char *json, size_t len)
{
    if (!dk_model_parse(json, len, s_model)) {
        ESP_LOGW(TAG, "ignored a message that isn't a snapshot");
        return;
    }
    if (lvgl_port_lock(2000)) {
        ui_apply_model(s_model);
        lvgl_port_unlock();
    }
}

// ----------------------------------------------------------- WebSocket

static void ws_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_websocket_event_data_t *ev = data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "relay connected");
        s_ws_up = true;
        set_conn(DK_CONN_ONLINE, NULL);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_ERROR:
        if (s_ws_up) ESP_LOGW(TAG, "relay connection lost, reconnecting");
        s_ws_up = false;
        set_conn(DK_CONN_RELAY, "Reconnecting to relay…");
        break;
    case WEBSOCKET_EVENT_DATA: {
        if (ev->op_code != 0x1 && ev->op_code != 0x0) break; // text frames (and continuations) only
        size_t total = ev->payload_len ? (size_t)ev->payload_len : (size_t)ev->data_len;
        if (total + 1 > s_rx_cap) {
            free(s_rx);
            s_rx_cap = total + 1024;
            s_rx = heap_caps_malloc(s_rx_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!s_rx) {
                s_rx_cap = 0;
                break;
            }
        }
        if ((size_t)ev->payload_offset + ev->data_len <= s_rx_cap) {
            memcpy(s_rx + ev->payload_offset, ev->data_ptr, ev->data_len);
        }
        if ((size_t)ev->payload_offset + ev->data_len >= total) {
            s_rx[total] = '\0';
            deliver_snapshot(s_rx, total);
        }
        break;
    }
    default:
        break;
    }
}

static char *ws_url(void)
{
    const char *u = CONFIG_DESKLING_RELAY_URL;
    char *out = malloc(strlen(u) + 16);
    if (strncmp(u, "https://", 8) == 0) sprintf(out, "wss://%s", u + 8);
    else if (strncmp(u, "http://", 7) == 0) sprintf(out, "ws://%s", u + 7);
    else strcpy(out, u);
    size_t n = strlen(out);
    while (n && out[n - 1] == '/') out[--n] = '\0';
    strcat(out, "/ws");
    return out;
}

static void ws_start(void)
{
    if (s_ws) return;
    static char headers[256];
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", CONFIG_DESKLING_TOKEN);
    char *url = ws_url();
    const esp_websocket_client_config_t cfg = {
        .uri = url,
        .headers = headers,
        .buffer_size = 4096,
        .task_stack = 8192,
        .reconnect_timeout_ms = 3000,
        .network_timeout_ms = 10000,
        .ping_interval_sec = 20,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    ESP_LOGI(TAG, "connecting to %s", url);
    s_ws = esp_websocket_client_init(&cfg);
    free(url);
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, ws_event, NULL);
    esp_websocket_client_start(s_ws);
}

void net_send(const char *json)
{
    if (!s_outbox || !json) return;
    char *copy = strdup(json);
    if (copy && xQueueSend(s_outbox, &copy, 0) != pdTRUE) free(copy);
}

static void sender_task(void *arg)
{
    (void)arg;
    for (;;) {
        char *msg;
        if (xQueueReceive(s_outbox, &msg, portMAX_DELAY) != pdTRUE) continue;
        // Wait (briefly) for the socket so a tap made while reconnecting still lands.
        for (int i = 0; i < 50 && !(s_ws && esp_websocket_client_is_connected(s_ws)); i++) vTaskDelay(pdMS_TO_TICKS(100));
        if (s_ws && esp_websocket_client_is_connected(s_ws)) {
            esp_websocket_client_send_text(s_ws, msg, strlen(msg), pdMS_TO_TICKS(3000));
        } else {
            ESP_LOGW(TAG, "dropped a message: relay offline");
        }
        free(msg);
    }
}

// ----------------------------------------------------------------- Wi-Fi

static void wifi_retry(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        set_conn(DK_CONN_WIFI, "Joining Wi-Fi…");
        esp_timer_start_once(s_wifi_retry, (uint64_t)s_wifi_backoff_ms * 1000);
        if (s_wifi_backoff_ms < 30000) s_wifi_backoff_ms *= 2;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Wi-Fi up");
        s_wifi_backoff_ms = 1000;
        if (!s_started_services) {
            s_started_services = true;
            esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            esp_netif_sntp_init(&sntp);
        }
        set_conn(DK_CONN_RELAY, NULL);
        ws_start();
    }
}

void net_start(void)
{
    s_model = heap_caps_calloc(1, sizeof(dk_model_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_outbox = xQueueCreate(16, sizeof(char *));
    xTaskCreate(sender_task, "dk_send", 4096, NULL, 5, NULL);
    dk_set_sender(net_send);

    if (strlen(CONFIG_DESKLING_WIFI_SSID) == 0 || strlen(CONFIG_DESKLING_RELAY_URL) == 0) {
        ESP_LOGW(TAG, "Wi-Fi or relay not configured: see firmware/README.md");
        set_conn(DK_CONN_SETUP, strlen(CONFIG_DESKLING_WIFI_SSID) ? "Relay URL not set" : "Wi-Fi not set up yet");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    const esp_timer_create_args_t t = {.callback = wifi_retry, .name = "wifi_retry"};
    ESP_ERROR_CHECK(esp_timer_create(&t, &s_wifi_retry));

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));

    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, CONFIG_DESKLING_WIFI_SSID, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, CONFIG_DESKLING_WIFI_PASSWORD, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = strlen(CONFIG_DESKLING_WIFI_PASSWORD) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    set_conn(DK_CONN_WIFI, "Joining Wi-Fi…");
    ESP_ERROR_CHECK(esp_wifi_start());
}
