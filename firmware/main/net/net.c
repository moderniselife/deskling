#include "net.h"
#include "inbox.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_tls_errors.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/apps/netbiosns.h"
#include "lwip/tcpip.h"
#include "mdns.h"
#include <strings.h>
#include "model.h"
#include "ota.h"
#include "portal.h"
#include "settings.h"
#include "ui.h"

static const char *TAG = "net";

typedef enum { CMD_SCAN, CMD_CONNECT, CMD_RELAY, CMD_STA_ONLY, CMD_RELAY_CHECK } cmd_kind_t;
typedef struct {
    cmd_kind_t kind;
    char a[128];
    char b[96];
} cmd_t;

static QueueHandle_t s_cmds, s_outbox;
static esp_netif_t *s_sta, *s_ap;
static esp_timer_handle_t s_announce;

static void netbios_start(void *arg)
{
    (void)arg;
    netbiosns_init();
    netbiosns_set_name("deskling");
}

/** Re-announce deskling.local now and then: phones and laptops that missed the first
 *  announcement (or flushed their cache) pick it up again without needing a query. */
static void mdns_announce(void *arg)
{
    (void)arg;
    if (s_sta) mdns_netif_action(s_sta, MDNS_EVENT_ANNOUNCE_IP4);
}
static esp_websocket_client_handle_t s_ws;
static esp_timer_handle_t s_wifi_retry;
static uint32_t s_backoff_ms = 1000;
static bool s_wifi_up, s_ws_up, s_services, s_ap_on;
static int64_t s_ws_since;   // when the relay client was (re)started or last connected, µs
static int64_t s_ws_last_rx; // anything heard from the relay (a pong counts), µs
static int64_t s_ws_last_app; // a message from the relay itself (snapshot, answer, pong), µs
static int64_t s_ws_pinged;   // when we last asked the relay for a pong, µs
static bool s_pong_seen;      // this relay answers our pings (older ones don't)
static uint32_t s_ws_restarts;
static char s_ip[16], s_code[8], s_ap_ssid[33];
static ui_wifi_ap_t s_scan[16];
static int s_scan_n;

// A join attempt started from the screen or the web page.
static struct {
    bool active;
    int tries;
    char ssid[33], pass[65];
    char prev_ssid[33], prev_pass[65];
} s_trial;

static char *s_rx;
static size_t s_rx_cap;

bool net_wifi_connected(void) { return s_wifi_up; }
bool net_relay_online(void) { return s_ws_up; }

// Why the relay connection last failed, in words a person can act on ("" while connected).
static char s_relay_err[160];
const char *net_relay_error(void) { return s_ws_up ? "" : s_relay_err; }

static void relay_failed(const esp_websocket_error_codes_t *e)
{
    char why[160];
    if (e->error_type == WEBSOCKET_ERROR_TYPE_HANDSHAKE || e->esp_ws_handshake_status_code) {
        int code = e->esp_ws_handshake_status_code;
        if (code == 401 || code == 403)
            snprintf(why, sizeof(why), "The relay didn't accept the token. Copy it again from the relay's /setup page.");
        else if (code == 404)
            snprintf(why, sizeof(why), "That URL isn't a Deskling relay (it answered 404). Check the relay URL.");
        else
            snprintf(why, sizeof(why), "The relay answered %d instead of letting Deskling in.", code);
    } else if (e->error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT) {
        esp_err_t t = e->esp_tls_last_esp_err;
        if (t == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME)
            snprintf(why, sizeof(why), "Couldn't look up the relay's address. Check the relay URL, or the network's DNS.");
        else if (t == ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST || t == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT)
            snprintf(why, sizeof(why), "Couldn't reach the relay (%s, errno %d).", esp_err_to_name(t), e->esp_transport_sock_errno);
        else if (t)
            snprintf(why, sizeof(why), "The secure connection to the relay failed (%s, tls 0x%x).", esp_err_to_name(t), e->esp_tls_stack_err);
        else
            snprintf(why, sizeof(why), "Lost the connection to the relay (errno %d).", e->esp_transport_sock_errno);
    } else if (e->error_type == WEBSOCKET_ERROR_TYPE_PONG_TIMEOUT) {
        snprintf(why, sizeof(why), "The relay stopped answering.");
    } else if (e->error_type == WEBSOCKET_ERROR_TYPE_SERVER_CLOSE) {
        snprintf(why, sizeof(why), "The relay closed the connection.");
    } else {
        return;
    }
    if (strcmp(why, s_relay_err)) ESP_LOGW(TAG, "relay: %s", why);
    snprintf(s_relay_err, sizeof(s_relay_err), "%s", why);
}
const char *net_ip(void) { return s_ip; }
const char *net_code(void) { return s_code; }
const char *net_ap_ssid(void) { return s_ap_on ? s_ap_ssid : ""; }

// Local-name check: ask the network what's around (Chromecasts, AirPlay speakers, printers,
// Macs...). Their replies come back as multicast, the same way deskling.local lookups arrive,
// so hearing nobody at all means multicast isn't reaching us (router or Wi-Fi chip), and
// hearing others means deskling.local itself should work.
static volatile int s_heard = -1;  // -1: not checked yet
static char s_heard_name[48];
static volatile bool s_checking;

static void netcheck_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(4000));
    static const char *types[][2] = {
        {"_googlecast", "_tcp"}, {"_airplay", "_tcp"}, {"_raop", "_tcp"}, {"_companion-link", "_tcp"},
        {"_ipp", "_tcp"}, {"_printer", "_tcp"}, {"_spotify-connect", "_tcp"}, {"_hap", "_tcp"},
        {"_sonos", "_tcp"}, {"_http", "_tcp"}, {"_smb", "_tcp"}, {"_device-info", "_tcp"},
        {"_matter", "_tcp"}, {"_meshcop", "_udp"}, {"_amzn-wplay", "_tcp"}, {"_workstation", "_tcp"},
    };
    int heard = 0;
    char name[48] = "";
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        mdns_result_t *res = NULL;
        if (mdns_query_ptr(types[i][0], types[i][1], 1200, 8, &res) != ESP_OK) continue;
        for (mdns_result_t *r = res; r; r = r->next) {
            const char *h = r->hostname ? r->hostname : "";
            if (!strcasecmp(h, "deskling")) continue;
            heard++;
            if (!name[0]) snprintf(name, sizeof(name), "%s", r->instance_name ? r->instance_name : h);
        }
        mdns_query_results_free(res);
    }
    snprintf(s_heard_name, sizeof(s_heard_name), "%s", name);
    s_heard = heard;
    ESP_LOGI(TAG, "Local-name check: heard %d device(s) on the network%s%s", heard, name[0] ? ", e.g. " : "", name);
    if (!heard) ESP_LOGW(TAG, "No multicast replies at all: deskling.local won't resolve from this network");
    s_checking = false;
    vTaskDelete(NULL);
}

void net_check_local_name(void)
{
    if (s_checking || !s_wifi_up) return;
    s_checking = true;
    if (xTaskCreate(netcheck_task, "netcheck", 4096, NULL, 2, NULL) != pdPASS) s_checking = false;
}

int net_local_name_heard(const char **example)
{
    if (example) *example = s_heard_name;
    return s_checking ? -2 : s_heard;
}


int net_scan_results(ui_wifi_ap_t *out, int max)
{
    int n = s_scan_n < max ? s_scan_n : max;
    memcpy(out, s_scan, sizeof(ui_wifi_ap_t) * n);
    return n;
}

// ------------------------------------------------------------- UI glue

static void set_conn(dk_conn_t c, const char *detail)
{
    if (lvgl_port_lock(1000)) {
        ui_set_connection(c, detail);
        lvgl_port_unlock();
    }
}

void net_push_info(void)
{
    const dk_settings_t *st = settings();
    ui_device_info_t info = {0};
    info.wifi_configured = settings_wifi_configured();
    info.wifi_connected = s_wifi_up;
    info.relay_configured = settings_relay_configured();
    snprintf(info.ssid, sizeof(info.ssid), "%s", st->wifi_ssid);
    snprintf(info.ip, sizeof(info.ip), "%s", s_ip);
    snprintf(info.hostname, sizeof(info.hostname), "%s", s_wifi_up ? "deskling.local" : "");
    snprintf(info.ap_ssid, sizeof(info.ap_ssid), "%s", net_ap_ssid());
    snprintf(info.code, sizeof(info.code), "%s", s_code);
    snprintf(info.relay_url, sizeof(info.relay_url), "%s", st->relay_url);
    snprintf(info.version, sizeof(info.version), "%s", esp_app_get_description()->version);
    snprintf(info.variant, sizeof(info.variant), "%s", ota_variant());
    info.brightness = st->brightness;
    info.volume = st->volume;
    info.quiet_night = st->quiet_night;
    info.rotate_180 = st->rotate_180;
    if (lvgl_port_lock(1000)) {
        ui_set_device_info(&info);
        lvgl_port_unlock();
    }
}

static void deliver_snapshot(const char *json, size_t len)
{
    if (len > 14 && strncmp(json, "{\"type\":\"pong\"", 15) == 0) {
        s_pong_seen = true; // the relay is really there; nothing for the screen
        return;
    }
    // Never wait for the screen here: the screen picks it up on its own task (ui/inbox.c).
    inbox_push(json, len);
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
        s_ws_since = s_ws_last_rx = s_ws_last_app = s_ws_pinged = esp_timer_get_time();
        s_pong_seen = false;
        s_relay_err[0] = '\0';
        set_conn(DK_CONN_ONLINE, NULL);
        dk_resend_view(); // the relay starts every connection with full chats until told otherwise
        ota_mark_healthy();
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_ERROR:
        if (ev) relay_failed(&ev->error_handle);
        if (s_ws_up) ESP_LOGW(TAG, "relay connection lost, reconnecting");
        s_ws_up = false;
        set_conn(DK_CONN_RELAY, "Reconnecting to relay…");
        break;
    case WEBSOCKET_EVENT_DATA: {
        s_ws_last_rx = esp_timer_get_time();
        if (ev->op_code == 0x1 || ev->op_code == 0x0) s_ws_last_app = s_ws_last_rx;
        if (ev->op_code != 0x1 && ev->op_code != 0x0) break;
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
        if ((size_t)ev->payload_offset + ev->data_len <= s_rx_cap) memcpy(s_rx + ev->payload_offset, ev->data_ptr, ev->data_len);
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

static void ws_stop(void)
{
    if (!s_ws) return;
    esp_websocket_client_stop(s_ws);
    esp_websocket_client_destroy(s_ws);
    s_ws = NULL;
    s_ws_up = false;
}

static void ws_start(void)
{
    if (s_ws || !s_wifi_up) return;
    if (!settings_relay_configured()) {
        set_conn(DK_CONN_SETUP, "Relay not set up yet");
        return;
    }
    const dk_settings_t *st = settings();
    static char headers[160];
    static char url[160];
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", st->token);
    const char *u = st->relay_url;
    if (strncmp(u, "https://", 8) == 0) snprintf(url, sizeof(url), "wss://%s/ws", u + 8);
    else if (strncmp(u, "http://", 7) == 0) snprintf(url, sizeof(url), "ws://%s/ws", u + 7);
    else snprintf(url, sizeof(url), "wss://%s/ws", u);
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
    s_ws_since = s_ws_last_rx = esp_timer_get_time();
    set_conn(DK_CONN_RELAY, NULL);
    s_ws = esp_websocket_client_init(&cfg);
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
        for (int i = 0; i < 50 && !(s_ws && esp_websocket_client_is_connected(s_ws)); i++) vTaskDelay(pdMS_TO_TICKS(100));
        if (s_ws && esp_websocket_client_is_connected(s_ws)) esp_websocket_client_send_text(s_ws, msg, strlen(msg), pdMS_TO_TICKS(3000));
        else ESP_LOGW(TAG, "dropped a message: relay offline");
        free(msg);
    }
}

// ----------------------------------------------------------------- Wi-Fi

static void post(cmd_kind_t kind, const char *a, const char *b)
{
    cmd_t c = {.kind = kind};
    if (a) snprintf(c.a, sizeof(c.a), "%s", a);
    if (b) snprintf(c.b, sizeof(c.b), "%s", b);
    xQueueSend(s_cmds, &c, 0);
}

void net_request_scan(void) { post(CMD_SCAN, NULL, NULL); }
void net_request_wifi(const char *ssid, const char *password) { post(CMD_CONNECT, ssid, password); }
void net_request_relay(const char *url, const char *token)
{
    settings_set_relay(url, token);
    post(CMD_RELAY, NULL, NULL);
}

static void sta_config(const char *ssid, const char *pass)
{
    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
}

static void start_hotspot(void)
{
    if (s_ap_on) return;
    if (!s_ap) s_ap = esp_netif_create_default_wifi_ap();
    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, s_ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(s_ap_ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN; // the code on screen guards the settings page
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) == ESP_OK && esp_wifi_set_config(WIFI_IF_AP, &ap) == ESP_OK) {
        s_ap_on = true;
        ESP_LOGI(TAG, "hotspot %s up (http://192.168.4.1)", s_ap_ssid);
    } else {
        ESP_LOGW(TAG, "couldn't start the setup hotspot; use the touchscreen");
    }
}

static void stop_hotspot(void)
{
    if (!s_ap_on) return;
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_ap_on = false;
    ESP_LOGI(TAG, "hotspot off");
}

static void trial_finish(bool ok, const char *msg)
{
    if (!s_trial.active) return;
    s_trial.active = false;
    if (ok) {
        settings_set_wifi(s_trial.ssid, s_trial.pass);
    } else if (s_trial.prev_ssid[0]) {
        sta_config(s_trial.prev_ssid, s_trial.prev_pass); // go back to the network that worked
        esp_wifi_connect();
    } else {
        start_hotspot();
    }
    if (lvgl_port_lock(1000)) {
        ui_wifi_connect_result(ok, msg);
        lvgl_port_unlock();
    }
    net_push_info();
}

static void wifi_retry(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (settings_wifi_configured() || s_trial.active) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        bool was_up = s_wifi_up;
        s_wifi_up = false;
        s_ip[0] = '\0';
        if (s_trial.active) {
            s_trial.tries++;
            bool bad_pw = d && (d->reason == WIFI_REASON_AUTH_FAIL || d->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                                d->reason == WIFI_REASON_HANDSHAKE_TIMEOUT || d->reason == WIFI_REASON_AUTH_EXPIRE);
            bool gone = d && d->reason == WIFI_REASON_NO_AP_FOUND;
            if (bad_pw && s_trial.tries >= 2) trial_finish(false, "That password didn't work. Try again?");
            else if (gone && s_trial.tries >= 3) trial_finish(false, "I can't see that network from here.");
            else if (s_trial.tries >= 5) trial_finish(false, "Couldn't join that network.");
            else esp_wifi_connect();
            return;
        }
        if (!settings_wifi_configured()) return;
        set_conn(DK_CONN_WIFI, "Joining Wi-Fi…");
        if (was_up) net_push_info();
        esp_timer_start_once(s_wifi_retry, (uint64_t)s_backoff_ms * 1000);
        if (s_backoff_ms < 30000) s_backoff_ms *= 2;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        uint16_t n = 0;
        esp_wifi_scan_get_ap_num(&n);
        if (n > 30) n = 30;
        wifi_ap_record_t *recs = calloc(n ? n : 1, sizeof(wifi_ap_record_t));
        ui_wifi_ap_t aps[16];
        int k = 0;
        if (recs && esp_wifi_scan_get_ap_records(&n, recs) == ESP_OK) {
            for (int i = 0; i < n && k < 16; i++) {
                if (!recs[i].ssid[0]) continue;
                bool dup = false;
                for (int j = 0; j < k; j++) dup |= strcmp(aps[j].ssid, (char *)recs[i].ssid) == 0;
                if (dup) continue; // records come strongest first
                snprintf(aps[k].ssid, sizeof(aps[k].ssid), "%s", (char *)recs[i].ssid);
                aps[k].rssi = recs[i].rssi;
                aps[k].secure = recs[i].authmode != WIFI_AUTH_OPEN;
                k++;
            }
        }
        free(recs);
        memcpy(s_scan, aps, sizeof(ui_wifi_ap_t) * k);
        s_scan_n = k;
        if (lvgl_port_lock(1000)) {
            ui_wifi_scan_done(aps, k);
            lvgl_port_unlock();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ip = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ip->ip_info.ip));
        ESP_LOGI(TAG, "Wi-Fi up: %s", s_ip);
        s_wifi_up = true;
        s_backoff_ms = 1000;
        // mDNS also hooks GOT_IP itself; doing it here too makes sure the STA side is on and
        // announced even if its handler raced ours.
        mdns_netif_action(s_sta, MDNS_EVENT_ENABLE_IP4 | MDNS_EVENT_ANNOUNCE_IP4);
        // Answer with the IPv4 address only. The board's pages are served over IPv4; a browser
        // that picks an IPv6 answer (Chrome tends to) would just time out.
        mdns_netif_action(s_sta, MDNS_EVENT_DISABLE_IP6);
        if (s_ap) mdns_netif_action(s_ap, MDNS_EVENT_DISABLE_IP4 | MDNS_EVENT_DISABLE_IP6);
        if (s_announce && !esp_timer_is_active(s_announce)) esp_timer_start_periodic(s_announce, 60 * 1000000ULL);
        net_check_local_name();
        if (s_trial.active) trial_finish(true, NULL);
        if (!settings_relay_configured()) ota_mark_healthy(); // nothing more to prove yet
        if (!s_services) {
            s_services = true;
            esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            esp_netif_sntp_init(&sntp);
            ota_check_soon();
        }
        net_push_info();
        if (s_ap_on) post(CMD_STA_ONLY, NULL, NULL);
        post(CMD_RELAY, NULL, NULL);
    }
}

static void net_task(void *arg)
{
    (void)arg;
    cmd_t c;
    for (;;) {
        if (xQueueReceive(s_cmds, &c, portMAX_DELAY) != pdTRUE) continue;
        switch (c.kind) {
        case CMD_SCAN: {
            wifi_scan_config_t sc = {.show_hidden = false};
            if (esp_wifi_scan_start(&sc, false) != ESP_OK && lvgl_port_lock(1000)) {
                ui_wifi_scan_done(NULL, 0);
                lvgl_port_unlock();
            }
            break;
        }
        case CMD_CONNECT:
            ESP_LOGI(TAG, "joining %s", c.a);
            snprintf(s_trial.prev_ssid, sizeof(s_trial.prev_ssid), "%s", settings()->wifi_ssid);
            snprintf(s_trial.prev_pass, sizeof(s_trial.prev_pass), "%s", settings()->wifi_pass);
            snprintf(s_trial.ssid, sizeof(s_trial.ssid), "%s", c.a);
            snprintf(s_trial.pass, sizeof(s_trial.pass), "%s", c.b);
            s_trial.tries = 0;
            s_trial.active = true;
            ws_stop();
            esp_wifi_disconnect();
            sta_config(c.a, c.b);
            esp_wifi_connect();
            break;
        case CMD_RELAY:
            ws_stop();
            ws_start();
            net_push_info();
            break;
        case CMD_RELAY_CHECK: {
            // The client reconnects by itself, but it can wedge: stuck "reconnecting" for good, or
            // connected to a link that has gone quiet. Start it again from scratch.
            if (!s_wifi_up || !s_ws || !settings_relay_configured()) break;
            int64_t now = esp_timer_get_time();
            bool stuck = !s_ws_up && now - s_ws_since > 45LL * 1000000;
            // Ask the relay itself every 30 s. A link can keep answering WebSocket pings while the
            // relay has lost it (then no updates arrive until a restart), so once the relay has
            // answered one of ours, its silence is what counts.
            if (s_ws_up && now - s_ws_pinged > 30LL * 1000000) {
                s_ws_pinged = now;
                net_send("{\"type\":\"ping\"}");
            }
            bool silent = s_ws_up && (s_pong_seen ? now - s_ws_last_app > 75LL * 1000000 : now - s_ws_last_rx > 75LL * 1000000);
            if (stuck || silent) {
                ESP_LOGW(TAG, "relay connection %s: starting it again", stuck ? "stuck" : "silent");
                s_ws_restarts++;
                ws_stop();
                ws_start();
            }
            break;
        }
        case CMD_STA_ONLY:
            vTaskDelay(pdMS_TO_TICKS(8000)); // let the web page show "done" before the hotspot goes
            if (s_wifi_up) stop_hotspot();
            net_push_info();
            break;
        }
    }
}

static void relay_watch(void *arg)
{
    (void)arg;
    post(CMD_RELAY_CHECK, NULL, NULL);
}

uint32_t net_relay_restarts(void) { return s_ws_restarts; }

void net_start(void)
{
    static esp_timer_handle_t watch;
    const esp_timer_create_args_t wa = {.callback = relay_watch, .name = "relay_watch"};
    if (!watch && esp_timer_create(&wa, &watch) == ESP_OK) esp_timer_start_periodic(watch, 10LL * 1000000);
    s_outbox = xQueueCreate(16, sizeof(char *));
    s_cmds = xQueueCreate(8, sizeof(cmd_t));
    xTaskCreate(sender_task, "dk_send", 4096, NULL, 5, NULL);
    xTaskCreate(net_task, "dk_net", 6144, NULL, 5, NULL);
    dk_set_sender(net_send);

    snprintf(s_code, sizeof(s_code), "%04u", (unsigned)(1000 + esp_random() % 9000));
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BASE);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "Deskling-%02X%02X", mac[4], mac[5]);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(s_sta, "deskling");
    const esp_timer_create_args_t t = {.callback = wifi_retry, .name = "wifi_retry"};
    ESP_ERROR_CHECK(esp_timer_create(&t, &s_wifi_retry));

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    if (mdns_init() == ESP_OK) {
        mdns_hostname_set("deskling");
        mdns_instance_name_set("Deskling");
        mdns_service_add("Deskling", "_http", "_tcp", 80, NULL, 0);
        const esp_timer_create_args_t at = {.callback = mdns_announce, .name = "mdns_announce"};
        esp_timer_create(&at, &s_announce);
    }
    // Windows also finds http://deskling through NetBIOS, which doesn't depend on multicast.
    tcpip_callback(netbios_start, NULL);

    if (settings_wifi_configured()) {
        sta_config(settings()->wifi_ssid, settings()->wifi_pass);
        set_conn(DK_CONN_WIFI, "Joining Wi-Fi…");
    } else {
        set_conn(DK_CONN_SETUP, "Wi-Fi not set up yet");
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    // Deskling is always plugged in, so keep the radio awake: in power save it sleeps through
    // most multicast, which is exactly what deskling.local lookups are.
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (!settings_wifi_configured()) start_hotspot();
    portal_start();
    net_push_info();
}
