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

typedef enum { SCR_HOME, SCR_SESSIONS, SCR_CHAT, SCR_ALERT, SCR_NIGHT, SCR_COUNT } scr_id_t;

/** The latest snapshot (never NULL after ui_init). */
extern const dk_model_t *g_model;
extern dk_conn_t g_conn;
extern char g_conn_detail[96];

void ui_go(scr_id_t id);
scr_id_t ui_current(void);
void ui_open_chat(const char *agent_id);
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
void scr_chat_refresh(dk_cause_t cause);

lv_obj_t *scr_alert_create(void);
/** Returns true if there is a request to show. */
bool scr_alert_refresh(void);
void scr_alert_tick(void);

lv_obj_t *scr_night_create(void);
void scr_night_refresh(void);
void scr_night_tick(void);

/** Shared widgets */
lv_obj_t *ui_nav_rail(lv_obj_t *parent, scr_id_t active);
void ui_nav_rail_update(lv_obj_t *rail);
lv_obj_t *ui_eq_bars(lv_obj_t *parent, int n, int32_t h, lv_color_t color);
lv_obj_t *ui_source_badge(lv_obj_t *parent, dk_source_t src);
