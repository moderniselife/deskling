#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "settings";
static const char *NS = "deskling";
static dk_settings_t s;

static void load_str(nvs_handle_t h, const char *key, char *dst, size_t cap, const char *fallback)
{
    size_t len = cap;
    if (h == 0 || nvs_get_str(h, key, dst, &len) != ESP_OK || dst[0] == '\0') {
        snprintf(dst, cap, "%s", fallback);
    }
}

static void apply_tz(void)
{
    setenv("TZ", s.tz, 1);
    tzset();
}

void settings_init(void)
{
    nvs_handle_t h = 0;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) h = 0;
    load_str(h, "ssid", s.wifi_ssid, sizeof(s.wifi_ssid), CONFIG_DESKLING_WIFI_SSID);
    load_str(h, "pass", s.wifi_pass, sizeof(s.wifi_pass), CONFIG_DESKLING_WIFI_PASSWORD);
    load_str(h, "relay", s.relay_url, sizeof(s.relay_url), CONFIG_DESKLING_RELAY_URL);
    load_str(h, "token", s.token, sizeof(s.token), CONFIG_DESKLING_TOKEN);
    load_str(h, "tz", s.tz, sizeof(s.tz), CONFIG_DESKLING_TZ);
    uint8_t b = 0;
    s.brightness = (h && nvs_get_u8(h, "bright", &b) == ESP_OK && b >= 10 && b <= 100) ? b : CONFIG_DESKLING_BRIGHTNESS;
    s.volume = (h && nvs_get_u8(h, "vol", &b) == ESP_OK && b <= 100) ? b : CONFIG_DESKLING_VOLUME;
    s.quiet_night = (h && nvs_get_u8(h, "quiet", &b) == ESP_OK) ? b != 0 : true;
    if (h) nvs_close(h);
    apply_tz();
    ESP_LOGI(TAG, "wifi %s, relay %s", s.wifi_ssid[0] ? s.wifi_ssid : "(none)", s.relay_url[0] ? s.relay_url : "(none)");
}

const dk_settings_t *settings(void)
{
    return &s;
}

static esp_err_t put_strs(const char *const *keys, const char *const *vals, int n)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    for (int i = 0; i < n && err == ESP_OK; i++) err = nvs_set_str(h, keys[i], vals[i]);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t settings_set_wifi(const char *ssid, const char *pass)
{
    snprintf(s.wifi_ssid, sizeof(s.wifi_ssid), "%s", ssid ? ssid : "");
    snprintf(s.wifi_pass, sizeof(s.wifi_pass), "%s", pass ? pass : "");
    const char *k[] = {"ssid", "pass"};
    const char *v[] = {s.wifi_ssid, s.wifi_pass};
    return put_strs(k, v, 2);
}

esp_err_t settings_set_relay(const char *url, const char *token)
{
    // Trim whitespace people paste along with the URL or token.
    char u[128], t[96];
    snprintf(u, sizeof(u), "%s", url ? url : "");
    snprintf(t, sizeof(t), "%s", token ? token : "");
    for (char *p = u + strlen(u); p > u && (p[-1] == ' ' || p[-1] == '\n' || p[-1] == '\r' || p[-1] == '/'); p--) p[-1] = '\0';
    for (char *p = t + strlen(t); p > t && (p[-1] == ' ' || p[-1] == '\n' || p[-1] == '\r'); p--) p[-1] = '\0';
    const char *us = u, *ts = t;
    while (*us == ' ') us++;
    while (*ts == ' ') ts++;
    snprintf(s.relay_url, sizeof(s.relay_url), "%s", us);
    snprintf(s.token, sizeof(s.token), "%s", ts);
    const char *k[] = {"relay", "token"};
    const char *v[] = {s.relay_url, s.token};
    return put_strs(k, v, 2);
}

esp_err_t settings_set_tz(const char *tz)
{
    if (!tz || !tz[0]) return ESP_ERR_INVALID_ARG;
    snprintf(s.tz, sizeof(s.tz), "%s", tz);
    apply_tz();
    const char *k[] = {"tz"};
    const char *v[] = {s.tz};
    return put_strs(k, v, 1);
}

esp_err_t settings_set_brightness(uint8_t percent)
{
    if (percent < 10) percent = 10;
    if (percent > 100) percent = 100;
    s.brightness = percent;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, "bright", percent);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t settings_set_sound(uint8_t volume, bool quiet_night)
{
    if (volume > 100) volume = 100;
    s.volume = volume;
    s.quiet_night = quiet_night;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, "vol", volume);
    if (err == ESP_OK) err = nvs_set_u8(h, "quiet", quiet_night ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t settings_reset(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_erase_all(h);
    err = nvs_commit(h);
    nvs_close(h);
    settings_init();
    return err;
}

bool settings_wifi_configured(void)
{
    return s.wifi_ssid[0] != '\0';
}

bool settings_relay_configured(void)
{
    return s.relay_url[0] != '\0' && s.token[0] != '\0';
}
