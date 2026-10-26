#include "ota.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "ui.h"

static const char *TAG = "ota";

#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
#define CHIP_PART "p4r1"
#else
#define CHIP_PART "p4r3"
#endif
#ifdef CONFIG_DESKLING_LCD_V2
#define LCD_PART "lcd2"
#else
#define LCD_PART "lcd1"
#endif

static char s_latest[32];
static char s_app_file[96];
static const char *const UPDATE_BASES[] = {CONFIG_DESKLING_UPDATE_URL, CONFIG_DESKLING_UPDATE_FALLBACK_URL};
static const char *s_base = CONFIG_DESKLING_UPDATE_URL; // the one that answered last
static bool s_busy, s_healthy;
static esp_timer_handle_t s_periodic, s_watchdog;

const char *ota_variant(void)
{
    return CHIP_PART "-" LCD_PART;
}

static void status(ui_update_state_t st, int pct, const char *msg)
{
    if (lvgl_port_lock(1000)) {
        ui_update_status(st, pct, s_latest, msg);
        lvgl_port_unlock();
    }
}

// "v1.2.3" → 1002003; anything else → -1
static long parse_ver(const char *v)
{
    if (!v) return -1;
    if (*v == 'v' || *v == 'V') v++;
    int a, b, c;
    char rest[16] = "";
    if (sscanf(v, "%d.%d.%d%15s", &a, &b, &c, rest) < 3 || rest[0]) return -1;
    return a * 1000000L + b * 1000L + c;
}

static bool is_newer(const char *latest, const char *current)
{
    long l = parse_ver(latest), c = parse_ver(current);
    if (l < 0) return false;
    if (c < 0) return strcmp(latest, current) != 0; // a dev build: offer the release
    return l > c;
}

// ---------------------------------------------------------------- check

static esp_err_t fetch_manifest_from(const char *base, char **out)
{
    char url[192];
    snprintf(url, sizeof(url), "%smanifest.json", base);
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .max_redirection_count = 5,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_open(c, 0);
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(c);
        int code = esp_http_client_get_status_code(c);
        if (code != 200) {
            err = ESP_FAIL;
        } else {
            size_t cap = 8192, len = 0;
            char *buf = malloc(cap);
            int r;
            while (buf && (r = esp_http_client_read(c, buf + len, cap - len - 1)) > 0) {
                len += r;
                if (len >= cap - 1) break;
            }
            if (buf) {
                buf[len] = '\0';
                *out = buf;
            } else {
                err = ESP_ERR_NO_MEM;
            }
        }
    }
    esp_http_client_cleanup(c);
    return err;
}

/** Try the update server, then the fallback. Remembers which one answered for the download. */
static esp_err_t fetch_manifest(char **out)
{
    for (size_t i = 0; i < sizeof(UPDATE_BASES) / sizeof(UPDATE_BASES[0]); i++) {
        if (!UPDATE_BASES[i][0]) continue;
        if (fetch_manifest_from(UPDATE_BASES[i], out) == ESP_OK && *out) {
            s_base = UPDATE_BASES[i];
            return ESP_OK;
        }
        ESP_LOGW(TAG, "no manifest at %s", UPDATE_BASES[i]);
    }
    return ESP_FAIL;
}

static void check_task(void *arg)
{
    bool quiet = (bool)(intptr_t)arg;
    if (!quiet) status(UI_UPDATE_CHECKING, 0, NULL);
    char *json = NULL;
    if (fetch_manifest(&json) != ESP_OK || !json) {
        if (!quiet) status(UI_UPDATE_FAILED, 0, "Couldn't reach the update server.");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
    cJSON *root = cJSON_Parse(json);
    free(json);
    const cJSON *ver = cJSON_GetObjectItem(root, "version");
    const cJSON *build = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "builds"), ota_variant());
    const cJSON *app = cJSON_GetObjectItem(build, "app");
    if (cJSON_IsString(ver) && cJSON_IsString(app)) {
        snprintf(s_latest, sizeof(s_latest), "%s", ver->valuestring);
        snprintf(s_app_file, sizeof(s_app_file), "%s", app->valuestring);
        const char *cur = esp_app_get_description()->version;
        if (is_newer(s_latest, cur)) {
            ESP_LOGI(TAG, "update available: %s (running %s)", s_latest, cur);
            status(UI_UPDATE_AVAILABLE, 0, NULL);
        } else if (!quiet) {
            status(UI_UPDATE_LATEST, 0, NULL);
        }
    } else if (!quiet) {
        status(UI_UPDATE_FAILED, 0, "No update published for this board yet.");
    }
    cJSON_Delete(root);
    s_busy = false;
    vTaskDelete(NULL);
}

static void start_check(bool quiet)
{
    if (s_busy) return;
    s_busy = true;
    xTaskCreate(check_task, "ota_check", 8192, (void *)(intptr_t)quiet, 4, NULL);
}

void ota_request_check(void)
{
    start_check(false);
}

static void periodic_cb(void *arg)
{
    (void)arg;
    start_check(true);
}

void ota_check_soon(void)
{
    if (!s_periodic) {
        const esp_timer_create_args_t t = {.callback = periodic_cb, .name = "ota_periodic"};
        esp_timer_create(&t, &s_periodic);
        esp_timer_start_periodic(s_periodic, 12ULL * 3600 * 1000000);
    }
    start_check(true);
}

// -------------------------------------------------------------- install

static void install_task(void *arg)
{
    (void)arg;
    char url[192];
    snprintf(url, sizeof(url), "%s%s", s_base, s_app_file);
    ESP_LOGI(TAG, "installing %s", url);
    status(UI_UPDATE_DOWNLOADING, 0, NULL);
    esp_http_client_config_t http = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
        .max_redirection_count = 5,
        .buffer_size = 4096,
    };
    esp_https_ota_config_t cfg = {.http_config = &http};
    esp_https_ota_handle_t h = NULL;
    esp_err_t err = esp_https_ota_begin(&cfg, &h);
    int last = -1;
    while (err == ESP_OK) {
        err = esp_https_ota_perform(h);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
        int total = esp_https_ota_get_image_size(h);
        int done = esp_https_ota_get_image_len_read(h);
        int pct = total > 0 ? (int)((int64_t)done * 100 / total) : 0;
        if (pct != last && pct % 2 == 0) {
            last = pct;
            status(UI_UPDATE_DOWNLOADING, pct, NULL);
        }
        err = ESP_OK;
    }
    if (err == ESP_OK && h && esp_https_ota_is_complete_data_received(h)) {
        err = esp_https_ota_finish(h);
        h = NULL;
    } else if (h) {
        esp_https_ota_abort(h);
        h = NULL;
        if (err == ESP_OK) err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        status(UI_UPDATE_DONE, 100, NULL);
        vTaskDelay(pdMS_TO_TICKS(2500)); // long enough for Deskling's "All done!"
        esp_restart();
    }
    ESP_LOGE(TAG, "update failed: %s", esp_err_to_name(err));
    status(UI_UPDATE_FAILED, 0, "The download failed. Try again?");
    s_busy = false;
    vTaskDelete(NULL);
}

void ota_request_install(void)
{
    if (s_busy || !s_app_file[0]) return;
    s_busy = true;
    xTaskCreate(install_task, "ota_install", 8192, NULL, 4, NULL);
}

// ------------------------------------------------------------- rollback

static void watchdog_cb(void *arg)
{
    (void)arg;
    if (s_healthy) return;
    ESP_LOGE(TAG, "new firmware never reached the relay: rolling back");
    esp_ota_mark_app_invalid_rollback_and_reboot();
}

void ota_init(void)
{
    esp_ota_img_states_t st;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "first boot after an update: keeping it only if it gets online within 3 minutes");
        const esp_timer_create_args_t t = {.callback = watchdog_cb, .name = "ota_watchdog"};
        esp_timer_create(&t, &s_watchdog);
        esp_timer_start_once(s_watchdog, 180ULL * 1000000);
    }
}

void ota_mark_healthy(void)
{
    if (s_healthy) return;
    s_healthy = true;
    if (s_watchdog) esp_timer_stop(s_watchdog);
    esp_ota_mark_app_valid_cancel_rollback();
}
