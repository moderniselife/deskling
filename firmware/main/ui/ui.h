// Public entry points for the Deskling UI. Call everything with the LVGL lock held.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "model.h"
#include "sound.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DK_CONN_SETUP,  // no Wi-Fi configured
    DK_CONN_WIFI,   // joining Wi-Fi
    DK_CONN_RELAY,  // Wi-Fi up, connecting to the relay
    DK_CONN_ONLINE, // live
} dk_conn_t;

typedef void (*ui_backlight_fn)(uint8_t percent);

void ui_init(void);
/** Hand the UI a freshly parsed snapshot (copied). */
void ui_apply_model(const dk_model_t *m);
void ui_set_connection(dk_conn_t c, const char *detail);
void ui_set_backlight_cb(ui_backlight_fn fn);

// ------------------------------------------------- device setup & settings

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} ui_wifi_ap_t;

/** What the device knows about itself; the UI shows setup until both are configured. */
typedef struct {
    bool wifi_configured;
    bool wifi_connected;
    bool relay_configured;
    char ssid[33];
    char ip[16];
    char hostname[32];  // "deskling.local"
    char ap_ssid[33];   // fallback hotspot while not set up, "" when off
    char code[8];       // shown on screen, typed on the web page to prove you can see it
    char relay_url[128];
    char version[32];
    char variant[24];
    uint8_t brightness; // 10..100
    uint8_t volume;     // 0..100
    bool quiet_night;
} ui_device_info_t;

typedef enum {
    UI_UPDATE_IDLE,
    UI_UPDATE_CHECKING,
    UI_UPDATE_LATEST,    // already up to date
    UI_UPDATE_AVAILABLE, // `latest` is newer
    UI_UPDATE_DOWNLOADING,
    UI_UPDATE_FAILED,
    UI_UPDATE_DONE,      // restarting into the new version
} ui_update_state_t;

/** Things the UI asks the device to do. Every call returns straight away; results come back below. */
typedef struct {
    void (*wifi_scan)(void);
    void (*wifi_connect)(const char *ssid, const char *password);
    void (*relay_set)(const char *url, const char *token);
    void (*check_update)(void);
    void (*start_update)(void);
    void (*set_brightness)(uint8_t percent); // also saves it
    void (*restart)(void);
    void (*play_sound)(dk_sound_t s);
    void (*set_sound)(uint8_t volume, bool quiet_night); // also saves it
    // Touch calibration. touch_raw returns true while a finger is down; NULL = no calibration support.
    bool (*touch_raw)(int32_t *a, int32_t *b);
    bool (*touch_calibrated)(void);
    void (*touch_save_calibration)(const float m[6]); // applies it straight away, and saves it
    // Fetch a chat picture from the relay (GET /img/<id>), shrink it to fit within
    // UI_IMAGE_MAX_W x UI_IMAGE_MAX_H, and hand it back with ui_image_ready. NULL = no pictures.
    void (*fetch_image)(const char *id);
} ui_platform_t;

#define UI_IMAGE_MAX_W 520
#define UI_IMAGE_MAX_H 360

void ui_set_platform(const ui_platform_t *p);
void ui_set_device_info(const ui_device_info_t *info);
void ui_wifi_scan_done(const ui_wifi_ap_t *aps, int n);
void ui_wifi_connect_result(bool ok, const char *message);
void ui_update_status(ui_update_state_t state, int progress, const char *latest, const char *message);
/** Show the touch calibration screen (e.g. from the web page when touch is too far off to use). */
void ui_start_touch_calibration(void);
/** A chat picture arrived (pixels are copied; NULL = it couldn't be fetched). Call with the LVGL lock held. */
void ui_image_ready(const char *id, const uint16_t *rgb565, int w, int h);
/** Play a sound, unless muted or it's quiet hours. */
void ui_sound(dk_sound_t s);

#ifdef __cplusplus
}
#endif
