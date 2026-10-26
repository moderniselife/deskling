// Settings kept in flash (NVS). Anything left empty falls back to the build-time
// value from menuconfig / sdkconfig.defaults.local, so both ways of setting up work.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char relay_url[128];
    char token[96];
    char tz[64];
    uint8_t brightness; // 10..100
    uint8_t volume;     // 0..100, 0 = silent
    bool quiet_night;   // between 10pm and 7am only reminders make a sound
} dk_settings_t;

/** Load once at boot (after nvs_flash_init). */
void settings_init(void);
/** The current settings (a live copy, read-only). */
const dk_settings_t *settings(void);
esp_err_t settings_set_wifi(const char *ssid, const char *pass);
esp_err_t settings_set_relay(const char *url, const char *token);
esp_err_t settings_set_tz(const char *tz);
esp_err_t settings_set_brightness(uint8_t percent);
esp_err_t settings_set_sound(uint8_t volume, bool quiet_night);
/** Forget everything set on the device (back to the build-time defaults). */
esp_err_t settings_reset(void);

bool settings_wifi_configured(void);
bool settings_relay_configured(void);
