#include "portal.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "device.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "ota.h"
#include "images.h"
#include "audio.h"
#include "board.h"
#include "esp_lvgl_port.h"
#include "ui.h"
#include "settings.h"
#include "sound.h"

static const char *TAG = "portal";

extern const char portal_html_start[] asm("_binary_portal_html_start");
extern const char portal_html_end[] asm("_binary_portal_html_end");

static int s_bad_tries;
static int64_t s_locked_until;
static char s_last_bad[8]; // the same wrong code again (a page still polling with an old one) isn't a new guess

// -------------------------------------------------------------- helpers

static esp_err_t send_json(httpd_req_t *req, cJSON *obj)
{
    char *s = cJSON_PrintUnformatted(obj);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, s ? s : "{}");
    cJSON_free(s);
    cJSON_Delete(obj);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg)
{
    httpd_resp_set_status(req, status);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "message", msg);
    return send_json(req, o);
}

static esp_err_t ok(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    return send_json(req, o);
}

/** The code on the screen proves you're in the room. Five different wrong codes lock for a minute;
    the right code always works once the minute is up, and repeating one wrong code (a browser tab
    still polling with the code from before a restart) counts once. */
static bool authorised(httpd_req_t *req)
{
    int64_t now = esp_timer_get_time();
    char code[8] = "";
    bool given = httpd_req_get_hdr_value_str(req, "X-Deskling-Code", code, sizeof(code)) == ESP_OK && code[0];
    if (given && strcmp(code, net_code()) == 0 && now >= s_locked_until) {
        s_bad_tries = 0;
        s_last_bad[0] = '\0';
        return true;
    }
    if (now < s_locked_until) {
        send_error(req, "429 Too Many Requests", "Too many wrong codes. Wait a minute.");
        return false;
    }
    if (given && strcmp(code, s_last_bad) != 0) {
        snprintf(s_last_bad, sizeof(s_last_bad), "%s", code);
        if (++s_bad_tries >= 5) {
            s_locked_until = now + 60LL * 1000000;
            s_bad_tries = 0;
        }
    }
    send_error(req, "401 Unauthorized", "That code doesn't match the screen.");
    return false;
}

static cJSON *read_json(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 2048) return cJSON_CreateObject();
    char *buf = calloc(1, req->content_len + 1);
    int got = 0;
    while (buf && got < req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r <= 0) break;
        got += r;
    }
    cJSON *o = buf ? cJSON_Parse(buf) : NULL;
    free(buf);
    return o ? o : cJSON_CreateObject();
}

static const char *str(cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItem(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

// -------------------------------------------------------------- handlers

static esp_err_t page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, portal_html_start, portal_html_end - portal_html_start);
}

static esp_err_t status(httpd_req_t *req)
{
    if (!authorised(req)) return ESP_OK;
    const dk_settings_t *st = settings();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "version", esp_app_get_description()->version);
    cJSON_AddStringToObject(o, "variant", ota_variant());
    cJSON_AddStringToObject(o, "ip", net_ip());
    cJSON_AddStringToObject(o, "tz", st->tz);
    cJSON_AddNumberToObject(o, "brightness", st->brightness);
    cJSON_AddNumberToObject(o, "volume", st->volume);
    cJSON_AddBoolToObject(o, "quiet_night", st->quiet_night);
    cJSON *w = cJSON_AddObjectToObject(o, "wifi");
    cJSON_AddStringToObject(w, "ssid", st->wifi_ssid);
    cJSON_AddBoolToObject(w, "connected", net_wifi_connected());
    cJSON_AddStringToObject(w, "ip", net_ip());
    cJSON_AddStringToObject(w, "hotspot", net_ap_ssid());
    const char *ex = "";
    cJSON *m = cJSON_AddObjectToObject(w, "local_name");
    cJSON_AddStringToObject(m, "name", "deskling.local");
    cJSON_AddNumberToObject(m, "heard", net_local_name_heard(&ex));
    cJSON_AddStringToObject(m, "example", ex);
    board_display_stats_t ds = board_display_stats();
    cJSON *d = cJSON_AddObjectToObject(o, "display");
    cJSON_AddNumberToObject(d, "frames", ds.frames);
    cJSON_AddNumberToObject(d, "avg_ms", (int)(ds.avg_ms * 10) / 10.0);
    cJSON_AddNumberToObject(d, "worst_ms", (int)(ds.worst_ms * 10) / 10.0);
    cJSON_AddNumberToObject(d, "busy_pct", ds.busy_pct);
    cJSON_AddNumberToObject(d, "draw_ms", (int)(ds.draw_ms * 10) / 10.0);
    cJSON_AddNumberToObject(d, "rotate_ms", (int)(ds.flush_ms * 10) / 10.0);
    cJSON_AddNumberToObject(d, "copy_wait_ms", (int)(ds.wait_ms * 10) / 10.0);
    cJSON_AddNumberToObject(d, "kpx", ds.kpx);
    cJSON_AddStringToObject(d, "bands", ds.internal_bands ? "internal" : "psram");
    cJSON_AddNumberToObject(d, "band_lines", ds.band_lines);
    images_stats_t is = images_stats();
    cJSON *im = cJSON_AddObjectToObject(o, "images");
    cJSON_AddNumberToObject(im, "requested", is.requested);
    cJSON_AddNumberToObject(im, "done", is.done);
    cJSON_AddNumberToObject(im, "failed", is.failed);
    cJSON_AddStringToObject(im, "last", is.last);
        cJSON *r = cJSON_AddObjectToObject(o, "relay");
    cJSON_AddStringToObject(r, "url", st->relay_url);
    cJSON_AddBoolToObject(r, "configured", settings_relay_configured());
    cJSON_AddBoolToObject(r, "online", net_relay_online());
    cJSON_AddStringToObject(r, "error", net_relay_error());
    size_t tl = strlen(st->token);
    cJSON_AddStringToObject(r, "token_end", tl >= 8 ? st->token + tl - 4 : "");
    return send_json(req, o);
}

static esp_err_t netcheck(httpd_req_t *req)
{
    if (!authorised(req)) return ESP_OK;
    net_check_local_name();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    return send_json(req, o);
}

static esp_err_t set_relay(httpd_req_t *req)
{
    if (!authorised(req)) return ESP_OK;
    cJSON *b = read_json(req);
    const char *url = str(b, "url");
    const char *token = str(b, "token");
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        cJSON_Delete(b);
        return send_error(req, "400 Bad Request", "The relay URL should start with https://");
    }
    if (!token[0]) token = settings()->token; // keep the current one
    if (!token[0]) {
        cJSON_Delete(b);
        return send_error(req, "400 Bad Request", "Paste the relay token too.");
    }
    net_request_relay(url, token);
    cJSON_Delete(b);
    return ok(req);
}

static esp_err_t set_wifi(httpd_req_t *req)
{
    if (!authorised(req)) return ESP_OK;
    cJSON *b = read_json(req);
    const char *ssid = str(b, "ssid");
    if (!ssid[0]) {
        cJSON_Delete(b);
        return send_error(req, "400 Bad Request", "Which network?");
    }
    net_request_wifi(ssid, str(b, "password"));
    cJSON_Delete(b);
    return ok(req);
}

static esp_err_t start_scan(httpd_req_t *req)
{
    if (!authorised(req)) return ESP_OK;
    net_request_scan();
    return ok(req);
}

static esp_err_t scan_results(httpd_req_t *req)
{
    if (!authorised(req)) return ESP_OK;
    ui_wifi_ap_t aps[16];
    int n = net_scan_results(aps, 16);
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(o, "networks");
    for (int i = 0; i < n; i++) {
        cJSON *a = cJSON_CreateObject();
        cJSON_AddStringToObject(a, "ssid", aps[i].ssid);
        cJSON_AddNumberToObject(a, "rssi", aps[i].rssi);
        cJSON_AddBoolToObject(a, "secure", aps[i].secure);
        cJSON_AddItemToArray(arr, a);
    }
    return send_json(req, o);
}

static esp_err_t set_tz(httpd_req_t *req)
{
    if (!authorised(req)) return ESP_OK;
    cJSON *b = read_json(req);
    esp_err_t err = settings_set_tz(str(b, "tz"));
    cJSON_Delete(b);
    return err == ESP_OK ? ok(req) : send_error(req, "400 Bad Request", "That doesn't look like a time zone.");
}

static esp_err_t set_brightness(httpd_req_t *req)
{
    if (!authorised(req)) return ESP_OK;
    cJSON *b = read_json(req);
    const cJSON *v = cJSON_GetObjectItem(b, "value");
    if (cJSON_IsNumber(v)) device_set_brightness((uint8_t)v->valueint);
    cJSON_Delete(b);
    return ok(req);
}

static esp_err_t touch_calibrate(httpd_req_t *req)
{
    if (!authorised(req)) return ESP_OK;
    if (lvgl_port_lock(1000)) {
        ui_start_touch_calibration();
        lvgl_port_unlock();
    }
    return ok(req);
}

/** {"volume":0..100, "quiet_night":bool, "play":"<sound name>"}: any subset. */
static esp_err_t set_sound(httpd_req_t *req)
{
    if (!authorised(req)) return ESP_OK;
    cJSON *b = read_json(req);
    const cJSON *v = cJSON_GetObjectItem(b, "volume");
    const cJSON *q = cJSON_GetObjectItem(b, "quiet_night");
    if (cJSON_IsNumber(v) || cJSON_IsBool(q)) {
        const dk_settings_t *st = settings();
        device_set_sound(cJSON_IsNumber(v) ? (uint8_t)v->valueint : st->volume, cJSON_IsBool(q) ? cJSON_IsTrue(q) : st->quiet_night);
    }
    const char *play = str(b, "play");
    for (int i = DK_SND_NONE + 1; play[0] && i < DK_SND_COUNT; i++) {
        if (strcmp(play, sound_name((dk_sound_t)i)) == 0) audio_play((dk_sound_t)i);
    }
    cJSON_Delete(b);
    return ok(req);
}

static void restart_later(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}

static esp_err_t restart(httpd_req_t *req)
{
    if (!authorised(req)) return ESP_OK;
    ok(req);
    xTaskCreate(restart_later, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static void show_progress(ui_update_state_t st, int pct, const char *version, const char *msg)
{
    if (lvgl_port_lock(500)) {
        ui_update_status(st, pct, version, msg);
        lvgl_port_unlock();
    }
}

static void restart_after_cheer(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(2500)); // long enough for Deskling's "All done!"
    esp_restart();
}

/** Firmware upload: the body is the raw app .bin. Deskling shows its install screen meanwhile. */
static esp_err_t update(httpd_req_t *req)
{
    if (!authorised(req)) return ESP_OK;
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part || req->content_len <= 0 || (size_t)req->content_len > part->size) {
        return send_error(req, "400 Bad Request", "That file doesn't fit. Is it the app .bin (not the -full one)?");
    }
    esp_ota_handle_t h;
    if (esp_ota_begin(part, req->content_len, &h) != ESP_OK) return send_error(req, "500 Internal Server Error", "Couldn't start the update.");
    char *buf = malloc(4096);
    int left = req->content_len;
    esp_err_t err = ESP_OK;
    bool checked = false;
    int shown = -1;
    char version[32] = "";
    while (left > 0 && err == ESP_OK) {
        int r = httpd_req_recv(req, buf, left < 4096 ? left : 4096);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) {
            err = ESP_FAIL;
            break;
        }
        if (!checked) {
            checked = true;
            if ((uint8_t)buf[0] != 0xE9) { // app images start with the ESP image magic
                err = ESP_ERR_INVALID_ARG;
                break;
            }
            // The app description (with its version) follows the image and first segment
            // headers: 24 + 8 bytes in, version at offset 16 of it.
            const int at = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + offsetof(esp_app_desc_t, version);
            if (r >= at + (int)sizeof(version)) {
                memcpy(version, buf + at, sizeof(version) - 1);
                version[sizeof(version) - 1] = 0;
                for (char *c = version; *c; c++)
                    if ((unsigned char)*c < 0x20 || (unsigned char)*c > 0x7e) *c = 0; // not a version after all
            }
            show_progress(UI_UPDATE_DOWNLOADING, 0, version[0] ? version : NULL, NULL);
        }
        err = esp_ota_write(h, buf, r);
        left -= r;
        int pct = (int)((int64_t)(req->content_len - left) * 100 / req->content_len);
        if (pct / 2 != shown / 2) {
            shown = pct;
            show_progress(UI_UPDATE_DOWNLOADING, pct, NULL, NULL);
        }
    }
    free(buf);
    if (err == ESP_OK) err = esp_ota_end(h);
    else esp_ota_abort(h);
    if (err == ESP_OK) err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "upload failed: %s", esp_err_to_name(err));
        const char *why = err == ESP_ERR_INVALID_ARG ? "That isn't a Deskling app image." : "The firmware didn't check out. Try downloading it again.";
        if (checked) show_progress(UI_UPDATE_FAILED, 0, NULL, why);
        return send_error(req, "400 Bad Request", why);
    }
    ESP_LOGI(TAG, "firmware uploaded, restarting");
    show_progress(UI_UPDATE_DONE, 100, NULL, NULL);
    ok(req);
    xTaskCreate(restart_after_cheer, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

void portal_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 16;
    cfg.recv_wait_timeout = 15;
    cfg.lru_purge_enable = true;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "couldn't start the settings page");
        return;
    }
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status},
        {.uri = "/api/netcheck", .method = HTTP_POST, .handler = netcheck},
        {.uri = "/api/relay", .method = HTTP_POST, .handler = set_relay},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = set_wifi},
        {.uri = "/api/scan", .method = HTTP_POST, .handler = start_scan},
        {.uri = "/api/scan", .method = HTTP_GET, .handler = scan_results},
        {.uri = "/api/tz", .method = HTTP_POST, .handler = set_tz},
        {.uri = "/api/brightness", .method = HTTP_POST, .handler = set_brightness},
        {.uri = "/api/sound", .method = HTTP_POST, .handler = set_sound},
        {.uri = "/api/touch/calibrate", .method = HTTP_POST, .handler = touch_calibrate},
        {.uri = "/api/restart", .method = HTTP_POST, .handler = restart},
        {.uri = "/api/update", .method = HTTP_POST, .handler = update},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) httpd_register_uri_handler(server, &routes[i]);
    ESP_LOGI(TAG, "settings page up on port 80");
}
