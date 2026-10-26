#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "anim.h"
#include "deskling.h"
#include "lvgl.h"
#include "model.h"
#include "theme.h"
#include "ui.h"

typedef enum { SCR_HOME, SCR_SESSIONS, SCR_CHAT, SCR_ALERT, SCR_NIGHT, SCR_SETUP, SCR_SETTINGS, SCR_BOOT, SCR_CALIB, SCR_UPDATE, SCR_NEW, SCR_COUNT } scr_id_t;

/** The latest snapshot (never NULL after ui_init). */
extern const dk_model_t *g_model;
extern dk_conn_t g_conn;
extern char g_conn_detail[96];
extern ui_device_info_t g_info;
extern const ui_platform_t *g_platform;
extern ui_update_state_t g_update_state;
extern int g_update_progress;
extern char g_update_latest[32];
extern char g_update_message[96];

void ui_go(scr_id_t id);
scr_id_t ui_current(void);
void ui_open_chat(const char *agent_id);
/** Go back one level (chat → where you came from, sessions/settings/night → home). False if there's nowhere to go. */
bool ui_back(void);

// Edge gestures (gestures.c)
void ui_gestures_init(void);
void ui_gestures_on_screen(scr_id_t id);
void ui_gestures_refresh(void);
bool ui_gestures_panel_open(void);
/** Slide the notification panel down (as if swiped). */
void ui_gestures_open_panel(void);
/** The alert screen finished its "Sent!" moment. */
void ui_alert_finished(void);
void ui_banner(const char *title, const char *body, lv_color_t bg);

/** Wall-clock milliseconds since the epoch, local. */
int64_t ui_now_ms(void);
/** Milliseconds to add to local time to get the relay's clock. */
int64_t ui_relay_offset_ms(void);
/** "now", "4m", "2h", "3d". */
void ui_ago(int64_t ts_ms, char *buf, size_t n);
/** e.g. "12 min ago" */
void ui_since(int64_t ts_ms, char *buf, size_t n);
void ui_clock_parts(char *hh, char *mm, char *ampm, char *date, size_t date_n);
void ui_due_text(int64_t due_ms, char *buf, size_t n);

/** Lead agent: the one the home screen talks about (needs you > waiting > working > newest). */
const dk_agent_t *ui_lead_agent(void);
dk_mood_t ui_mood_for(const dk_agent_t *a);
const char *ui_headline_for(const dk_agent_t *a, char *buf, size_t n);
/** Count agents in a state. */
int ui_count_state(dk_state_t s);

// Screens
lv_obj_t *scr_home_create(void);
void scr_home_refresh(dk_cause_t cause, const char *cause_id);
void scr_home_tick(void);

lv_obj_t *scr_sessions_create(void);
void scr_sessions_refresh(void);

lv_obj_t *scr_chat_create(void);
void scr_chat_show(const char *agent_id);
const char *scr_chat_agent(void);
void scr_chat_refresh(dk_cause_t cause);
/* The answer to "Show all" or an opened steps group; may take d's text and steps. */
void scr_chat_detail(dk_detail_t *d);
/* Swipe back on the chat: closes the reader first. */
bool scr_chat_back(void);

lv_obj_t *scr_alert_create(void);
/** Returns true if there is a request to show. */
bool scr_alert_refresh(void);
void scr_alert_tick(void);

lv_obj_t *scr_night_create(void);
void scr_night_refresh(void);
void scr_night_tick(void);

lv_obj_t *scr_setup_create(void);
void scr_setup_refresh(void);
/** Jump straight to the Wi-Fi list (from Settings). */
void scr_setup_wifi(void);
void scr_setup_scan_done(const ui_wifi_ap_t *aps, int n);
void scr_setup_connect_result(bool ok, const char *message);

lv_obj_t *scr_settings_create(void);

// Boot animation (scr_boot.c): plays once, then calls done.
lv_obj_t *scr_boot_create(void);
void scr_boot_play(void (*done)(void));

// Touch calibration (scr_calib.c): five taps, then a check. Calls done when finished.
#define CALIB_POINTS 5
extern const lv_point_t g_calib_targets[CALIB_POINTS];
lv_obj_t *scr_calib_create(void);
// Installing an update (scr_update.c): shows g_update_state / g_update_progress.
lv_obj_t *scr_update_create(void (*on_back)(void));
void scr_update_refresh(void);
void scr_calib_start(void (*done)(void));
void scr_settings_refresh(void);

/** Shared widgets */
/** A QR code for `text`, `size` px square, with a white quiet zone. */
lv_obj_t *ui_qr(lv_obj_t *parent, const char *text, int32_t size);
/** "http://deskling.local" (or the IP when mDNS isn't up yet). */
const char *ui_portal_url(void);
lv_obj_t *ui_nav_rail(lv_obj_t *parent, scr_id_t active);
void ui_nav_rail_update(lv_obj_t *rail);
lv_obj_t *ui_eq_bars(lv_obj_t *parent, int n, int32_t h, lv_color_t color);
lv_obj_t *ui_source_badge(lv_obj_t *parent, dk_source_t src);

/* Chat pictures (images.c): returns the picture once it's here, and starts fetching it if not. */
const lv_image_dsc_t *ui_image_get(const char *id, bool *failed);
void scr_chat_images_changed(void);

/* New session (scr_new.c) */
lv_obj_t *scr_new_create(void);
void scr_new_refresh(void);
