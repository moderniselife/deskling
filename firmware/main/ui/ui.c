#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "ui_internal.h"

#define NIGHT_AFTER_IDLE_MS (5 * 60 * 1000)
#define NIGHT_FROM_HOUR 22
#define NIGHT_UNTIL_HOUR 7

static dk_model_t *s_model;
const dk_model_t *g_model;
dk_conn_t g_conn = DK_CONN_WIFI;
char g_conn_detail[96];
ui_device_info_t g_info = {.brightness = 90};
const ui_platform_t *g_platform;
ui_update_state_t g_update_state = UI_UPDATE_IDLE;
int g_update_progress;
char g_update_latest[32];
char g_update_message[96];
static bool s_have_info;

static lv_obj_t *s_scr[SCR_COUNT];
static scr_id_t s_cur = SCR_HOME;
static scr_id_t s_before_alert = SCR_HOME;
static int64_t s_relay_offset;
static ui_backlight_fn s_backlight;
static uint8_t s_backlight_pct = 100;
static lv_obj_t *s_banner;

// --------------------------------------------------------------- time

int64_t ui_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int64_t ui_relay_offset_ms(void)
{
    return s_relay_offset;
}

void ui_ago(int64_t ts_ms, char *buf, size_t n)
{
    int64_t s = (ui_now_ms() + s_relay_offset - ts_ms) / 1000;
    if (s < 60) snprintf(buf, n, "now");
    else if (s < 3600) snprintf(buf, n, "%dm", (int)(s / 60));
    else if (s < 86400) snprintf(buf, n, "%dh", (int)(s / 3600));
    else snprintf(buf, n, "%dd", (int)(s / 86400));
}

void ui_since(int64_t ts_ms, char *buf, size_t n)
{
    int64_t s = (ui_now_ms() + s_relay_offset - ts_ms) / 1000;
    if (s < 60) snprintf(buf, n, "just now");
    else if (s < 3600) snprintf(buf, n, "%d min ago", (int)(s / 60));
    else snprintf(buf, n, "%d h ago", (int)(s / 3600));
}

void ui_clock_parts(char *hh, char *mm, char *ampm, char *date, size_t date_n)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    int h = tm.tm_hour % 12;
    sprintf(hh, "%d", h == 0 ? 12 : h);
    sprintf(mm, "%02d", tm.tm_min);
    strcpy(ampm, tm.tm_hour < 12 ? "am" : "pm");
    if (date) {
        static const char *days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        static const char *months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
        snprintf(date, date_n, "%s %d %s", days[tm.tm_wday], tm.tm_mday, months[tm.tm_mon]);
    }
}

void ui_due_text(int64_t due_ms, char *buf, size_t n)
{
    if (due_ms <= 0) {
        snprintf(buf, n, "Any time");
        return;
    }
    time_t t = (time_t)((due_ms - s_relay_offset) / 1000);
    struct tm tm, now_tm;
    localtime_r(&t, &tm);
    time_t now = time(NULL);
    localtime_r(&now, &now_tm);
    int h = tm.tm_hour % 12;
    const char *day = (tm.tm_yday == now_tm.tm_yday) ? "" : (tm.tm_yday == now_tm.tm_yday + 1 ? "Tomorrow " : "");
    snprintf(buf, n, "%s%d:%02d %s", day, h == 0 ? 12 : h, tm.tm_min, tm.tm_hour < 12 ? "am" : "pm");
}

// ------------------------------------------------------------ helpers

const dk_agent_t *ui_lead_agent(void)
{
    // The relay already sorts: needs you, waiting, working, done, idle; newest first.
    return g_model->agent_n ? &g_model->agents[0] : NULL;
}

int ui_count_state(dk_state_t s)
{
    int n = 0;
    for (int i = 0; i < g_model->agent_n; i++) n += g_model->agents[i].state == s;
    return n;
}

dk_mood_t ui_mood_for(const dk_agent_t *a)
{
    if (!a) return DK_MOOD_CALM;
    switch (a->state) {
    case DK_WORKING: return DK_MOOD_BUSY;
    case DK_NEEDS_YOU: return DK_MOOD_WORRIED;
    case DK_WAITING: return DK_MOOD_CURIOUS;
    default: return DK_MOOD_CALM;
    }
}

const char *ui_headline_for(const dk_agent_t *a, char *buf, size_t n)
{
    if (!a) {
        snprintf(buf, n, "All quiet");
        return buf;
    }
    const char *who = a->source == DK_SRC_CUSTOM ? a->name : (a->source == DK_SRC_CLAUDE ? "Claude" : dk_source_label(a->source));
    switch (a->state) {
    case DK_WORKING:
        if (ui_count_state(DK_WORKING) > 1) snprintf(buf, n, "%d agents busy", ui_count_state(DK_WORKING));
        else snprintf(buf, n, "%s is busy", who);
        break;
    case DK_NEEDS_YOU: snprintf(buf, n, "%s needs you", who); break;
    case DK_WAITING: snprintf(buf, n, "Your turn"); break;
    case DK_DONE: snprintf(buf, n, "All done!"); break;
    default: snprintf(buf, n, "Ready when you are"); break;
    }
    return buf;
}

lv_obj_t *ui_eq_bars(lv_obj_t *parent, int n, int32_t h, lv_color_t color)
{
    lv_obj_t *row = dk_box(parent);
    lv_obj_set_height(row, h);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 5, 0);
    static const uint16_t delays[] = {0, 200, 400, 100, 300, 150};
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = dk_box(row);
        lv_obj_set_size(b, 7, h);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_set_style_bg_color(b, color, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        dk_anim_eq(b, h, 900, delays[i % 6]);
    }
    return row;
}

lv_obj_t *ui_source_badge(lv_obj_t *parent, dk_source_t src)
{
    lv_obj_t *pill = dk_card(parent, DK_CARD, LV_RADIUS_CIRCLE);
    lv_obj_set_style_pad_left(pill, 6, 0);
    lv_obj_set_style_pad_right(pill, 12, 0);
    lv_obj_set_style_pad_ver(pill, 5, 0);
    dk_row(pill, 8);
    lv_obj_t *sq = dk_card(pill, src == DK_SRC_CURSOR ? DK_INK : (src == DK_SRC_CLAUDE ? lv_color_hex(0xF4C7AE) : DK_LILAC), 8);
    lv_obj_set_size(sq, 26, 26);
    lv_obj_set_style_pad_all(sq, 0, 0);
    const char *init = src == DK_SRC_CURSOR ? "Cu" : src == DK_SRC_CODEX ? "Cx" : src == DK_SRC_GEMINI ? "G" : src == DK_SRC_CUSTOM ? "A" : "C";
    lv_obj_t *l = dk_label(sq, DK_FONT_XS, src == DK_SRC_CURSOR ? DK_GROUND : DK_INK, init);
    lv_obj_center(l);
    dk_label(pill, DK_FONT_XS, DK_INK, dk_source_label(src));
    return pill;
}

// Rail used by Sessions (and anything else that wants it).
static void rail_click(lv_event_t *e)
{
    ui_go((scr_id_t)(intptr_t)lv_event_get_user_data(e));
}

lv_obj_t *ui_nav_rail(lv_obj_t *parent, scr_id_t active)
{
    lv_obj_t *rail = dk_box(parent);
    lv_obj_set_width(rail, 104);
    lv_obj_set_height(rail, lv_pct(100));
    dk_col(rail, 14);
    lv_obj_set_flex_align(rail, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(rail, 6, 0);
    dk_label(rail, DK_FONT_XS, DK_PURPLE, "deskling");
    static const struct {
        scr_id_t id;
        const char *icon;
        const char *label;
    } items[] = {{SCR_HOME, LV_SYMBOL_HOME, "Home"},
                 {SCR_SESSIONS, LV_SYMBOL_LIST, "Sessions"},
                 {SCR_NIGHT, LV_SYMBOL_EYE_CLOSE, "Night"},
                 {SCR_SETTINGS, LV_SYMBOL_SETTINGS, "Settings"}};
    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
        bool on = items[i].id == active;
        lv_obj_t *b = lv_button_create(rail);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 80, 80);
        lv_obj_set_style_radius(b, 26, 0);
        lv_obj_set_style_bg_color(b, DK_INK, 0);
        lv_obj_set_style_bg_opa(b, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        dk_col(b, 2);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        dk_label(b, &lv_font_montserrat_28, on ? DK_GROUND : DK_MUTED, items[i].icon);
        dk_label(b, DK_FONT_XS, on ? DK_GROUND : DK_MUTED, items[i].label);
        lv_obj_add_event_cb(b, rail_click, LV_EVENT_CLICKED, (void *)(intptr_t)items[i].id);
        dk_press_style(b);
        if (items[i].id == SCR_SESSIONS) {
            lv_obj_t *badge = dk_card(b, DK_ALERT_INK, LV_RADIUS_CIRCLE);
            lv_obj_add_flag(badge, LV_OBJ_FLAG_FLOATING);
            lv_obj_set_size(badge, 26, 26);
            lv_obj_set_style_pad_all(badge, 0, 0);
            lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -4, 4);
            lv_obj_t *n = dk_label(badge, DK_FONT_XS, lv_color_white(), "");
            lv_obj_center(n);
            lv_obj_set_user_data(rail, badge);
        }
    }
    ui_nav_rail_update(rail);
    return rail;
}

void ui_nav_rail_update(lv_obj_t *rail)
{
    lv_obj_t *badge = lv_obj_get_user_data(rail);
    if (!badge) return;
    int need = ui_count_state(DK_NEEDS_YOU) + ui_count_state(DK_WAITING);
    if (need == 0) {
        lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(lv_obj_get_child(badge, 0), "%d", need);
}

// --------------------------------------------------------- navigation

scr_id_t ui_current(void)
{
    return s_cur;
}

static void set_backlight(uint8_t pct)
{
    if (s_backlight && pct != s_backlight_pct) s_backlight(pct);
    s_backlight_pct = pct;
}


// ------------------------------------------------------- screen changes
// Same motion as lv_screen_load_anim (linear fade / slide / cover), but the outgoing screen
// is captured once as a picture (the "ghost") instead of being re-rendered on every frame of
// the transition. LVGL would redraw both screens in full for every frame of a fade, with the
// incoming one blended on top; this way only the incoming screen is live, so its own entrance
// animations still play, and the outgoing one costs a single image blit per frame.
static lv_draw_buf_t *s_ghost_buf;
static lv_obj_t *s_ghost, *s_trans_scr;

static void trans_x(void *o, int32_t v) { lv_obj_set_x(o, v); }
static void trans_y(void *o, int32_t v) { lv_obj_set_y(o, v); }
static void trans_opa(void *o, int32_t v) { dk_style(o, LV_STYLE_OPA, v); }

static void trans_finish(void)
{
    if (s_ghost) {
        lv_anim_delete(s_ghost, NULL);
        lv_obj_add_flag(s_ghost, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_trans_scr) {
        lv_anim_delete(s_trans_scr, trans_x);
        lv_anim_delete(s_trans_scr, trans_y);
        lv_obj_set_pos(s_trans_scr, 0, 0);
        s_trans_scr = NULL;
    }
}

static void trans_done(lv_anim_t *a)
{
    (void)a;
    trans_finish();
}

/** Capture the current screen into the ghost picture. False if there's no memory for it. */
static bool ghost_capture(lv_obj_t *scr)
{
#if !LV_USE_SNAPSHOT
    (void)scr;
    return false;
#else
    int32_t w = lv_display_get_horizontal_resolution(NULL), h = lv_display_get_vertical_resolution(NULL);
    if (!s_ghost_buf) {
        s_ghost_buf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
        if (!s_ghost_buf) return false;
    }
    if (lv_snapshot_take_to_draw_buf(scr, LV_COLOR_FORMAT_RGB565, s_ghost_buf) != LV_RESULT_OK) return false;
    if (!s_ghost) {
        s_ghost = lv_image_create(lv_layer_top());
        lv_obj_remove_flag(s_ghost, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_ghost, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
    }
#if LV_CACHE_DEF_SIZE > 0
    lv_image_cache_drop(s_ghost_buf); // same buffer, new picture
#endif
    lv_image_set_src(s_ghost, s_ghost_buf);
    lv_obj_invalidate(s_ghost);
    return true;
#endif
}

static void ghost_anim(lv_obj_t *o, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms, bool last)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_early_apply(&a, true);
    if (last) lv_anim_set_completed_cb(&a, trans_done);
    lv_anim_start(&a);
}

static void show_screen(lv_obj_t *scr, lv_screen_load_anim_t how, uint32_t ms)
{
    trans_finish();
    lv_obj_t *old = lv_screen_active();
    if (!old || old == scr || !ghost_capture(old)) {
        lv_screen_load_anim(scr, how, ms, 0, false);
        return;
    }
    int32_t w = lv_display_get_horizontal_resolution(NULL), h = lv_display_get_vertical_resolution(NULL);
    // Fades put the ghost over the new screen; slides and covers put it underneath.
    bool over = how == LV_SCREEN_LOAD_ANIM_FADE_IN;
    lv_obj_set_parent(s_ghost, over ? lv_layer_top() : lv_layer_bottom());
    lv_obj_move_to_index(s_ghost, 0);
    lv_obj_set_pos(s_ghost, 0, 0);
    lv_obj_remove_local_style_prop(s_ghost, LV_STYLE_OPA, 0);
    lv_obj_remove_flag(s_ghost, LV_OBJ_FLAG_HIDDEN);

    lv_screen_load(scr);
    s_trans_scr = scr;
    switch (how) {
    case LV_SCREEN_LOAD_ANIM_MOVE_LEFT:
        ghost_anim(s_ghost, trans_x, 0, -w, ms, false);
        ghost_anim(scr, trans_x, w, 0, ms, true);
        break;
    case LV_SCREEN_LOAD_ANIM_MOVE_RIGHT:
        ghost_anim(s_ghost, trans_x, 0, w, ms, false);
        ghost_anim(scr, trans_x, -w, 0, ms, true);
        break;
    case LV_SCREEN_LOAD_ANIM_OVER_TOP:
        ghost_anim(scr, trans_y, h, 0, ms, true);
        break;
    default: // fade
        ghost_anim(s_ghost, trans_opa, LV_OPA_COVER, LV_OPA_TRANSP, ms, true);
        break;
    }
}

// While the boot animation or touch setup is showing, anything else that wants the screen
// waits its turn: the latest request is remembered and shown afterwards.
static bool s_holding;
static scr_id_t s_after_hold = SCR_HOME;

void ui_go(scr_id_t id)
{
    if (s_holding && id != SCR_BOOT && id != SCR_CALIB && id != SCR_UPDATE) {
        s_after_hold = id;
        return;
    }
    if (id == s_cur) return;
    if (id == SCR_ALERT && s_cur != SCR_ALERT) s_before_alert = (s_cur == SCR_BOOT || s_cur == SCR_CALIB) ? SCR_HOME : s_cur;
    lv_screen_load_anim_t how = LV_SCREEN_LOAD_ANIM_FADE_IN;
    uint32_t ms = 250;
    if (id == SCR_ALERT) how = LV_SCREEN_LOAD_ANIM_OVER_TOP, ms = 380;
    else if (s_cur == SCR_ALERT) how = LV_SCREEN_LOAD_ANIM_FADE_IN, ms = 300;
    else if (id == SCR_CHAT) how = LV_SCREEN_LOAD_ANIM_MOVE_LEFT, ms = 320;
    else if (s_cur == SCR_CHAT) how = LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, ms = 320;
    else if (id == SCR_NIGHT || s_cur == SCR_NIGHT) how = LV_SCREEN_LOAD_ANIM_FADE_IN, ms = 700;
    s_cur = id;
    set_backlight(id == SCR_NIGHT ? 25 : 100);
    if (id == SCR_SESSIONS) scr_sessions_refresh();
    if (id == SCR_NIGHT) scr_night_refresh();
    if (id == SCR_SETTINGS) scr_settings_refresh();
    if (id == SCR_SETUP) scr_setup_refresh();
    if (id == SCR_HOME) scr_home_refresh(DK_CAUSE_HELLO, "");
    if (id == SCR_CHAT) scr_chat_refresh(DK_CAUSE_HELLO);
    show_screen(s_scr[id], how, ms);
    ui_gestures_on_screen(id);
}

void ui_alert_finished(void)
{
    if (!scr_alert_refresh() && s_cur == SCR_ALERT) ui_go(s_before_alert == SCR_NIGHT ? SCR_HOME : s_before_alert);
}

static scr_id_t s_chat_from = SCR_SESSIONS;

bool ui_back(void)
{
    switch (s_cur) {
    case SCR_CHAT:
        ui_go(s_chat_from == SCR_CHAT || s_chat_from == SCR_ALERT ? SCR_SESSIONS : s_chat_from);
        return true;
    case SCR_SESSIONS:
    case SCR_SETTINGS:
    case SCR_NIGHT:
        ui_go(SCR_HOME);
        return true;
    default:
        return false;
    }
}

void ui_open_chat(const char *agent_id)
{
    if (s_cur != SCR_CHAT) s_chat_from = s_cur;
    scr_chat_show(agent_id);
    ui_go(SCR_CHAT);
}

// ------------------------------------------------------------- banner

static void banner_gone(lv_anim_t *a)
{
    lv_obj_t *o = lv_anim_get_user_data(a);
    if (o == s_banner) s_banner = NULL;
    lv_obj_delete(o);
}

static void banner_opa(void *o, int32_t v)
{
    dk_style(o, LV_STYLE_OPA, (lv_opa_t)v);
}

static void banner_tap(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_current_target(e);
    lv_anim_delete(b, NULL);
    if (b == s_banner) s_banner = NULL;
    lv_obj_delete_async(b);
    ui_gestures_open_panel();
}

static void banner(const char *title, const char *body, lv_color_t bg, const char *icon_sym)
{
    if (s_banner) lv_obj_delete(s_banner);
    s_banner = dk_card(lv_layer_top(), bg, 28);
    lv_obj_add_flag(s_banner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_banner, banner_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_pad_hor(s_banner, 26, 0);
    lv_obj_set_style_pad_ver(s_banner, 16, 0);
    lv_obj_set_style_shadow_width(s_banner, 30, 0);
    lv_obj_set_style_shadow_opa(s_banner, LV_OPA_20, 0);
    lv_obj_set_style_shadow_offset_y(s_banner, 8, 0);
    dk_row(s_banner, 16);
    lv_obj_align(s_banner, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_t *icon = dk_card(s_banner, DK_INK, LV_RADIUS_CIRCLE);
    lv_obj_set_size(icon, 48, 48);
    lv_obj_set_style_pad_all(icon, 0, 0);
    lv_obj_t *il = dk_label(icon, &lv_font_montserrat_20, DK_BUTTER, icon_sym);
    lv_obj_center(il);
    dk_anim_wiggle(icon);
    lv_obj_t *txt = dk_box(s_banner);
    dk_col(txt, 2);
    dk_label(txt, DK_FONT_XL, DK_INK, title);
    if (body && body[0]) {
        lv_obj_t *b = dk_label(txt, DK_FONT_M, DK_INK_SOFT, body);
        lv_label_set_long_mode(b, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_max_width(b, 760, 0);
    }
    dk_anim_drop_in(s_banner, -60, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_banner);
    lv_anim_set_exec_cb(&a, banner_opa);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, 400);
    lv_anim_set_delay(&a, 4500);
    lv_anim_set_user_data(&a, s_banner);
    lv_anim_set_completed_cb(&a, banner_gone);
    lv_anim_start(&a);
}

void ui_banner(const char *title, const char *body, lv_color_t bg)
{
    banner(title, body, bg, LV_SYMBOL_BELL);
}

// ---------------------------------------------------------- lifecycle

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    scr_home_tick();
    scr_night_tick();
    scr_alert_tick();

    // Drift to the night clock when it's late and nothing needs you.
    if (s_cur == SCR_HOME || s_cur == SCR_SESSIONS) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        bool late = tm.tm_hour >= NIGHT_FROM_HOUR || tm.tm_hour < NIGHT_UNTIL_HOUR;
        bool calm = g_model->pending_n == 0 && ui_count_state(DK_NEEDS_YOU) == 0;
        if (late && calm && lv_display_get_inactive_time(NULL) > NIGHT_AFTER_IDLE_MS) {
            ui_sound(DK_SND_SLEEPY);
            ui_go(SCR_NIGHT);
        }
    }
}

static void release_hold(void)
{
    s_holding = false;
    scr_id_t next = s_after_hold;
    if (next == SCR_HOME) scr_home_refresh(DK_CAUSE_HELLO, "");
    ui_go(next);
}

static void calib_done(void)
{
    ui_banner("Touch is all set", "You can redo it any time from Settings.", DK_GOOD_BG);
    release_hold();
}

static void update_back(void)
{
    g_update_state = UI_UPDATE_IDLE;
    scr_settings_refresh();
    release_hold();
}

static void boot_done(void)
{
    bool need_cal = g_platform && g_platform->touch_raw && g_platform->touch_calibrated && !g_platform->touch_calibrated();
    if (need_cal) {
        ui_go(SCR_CALIB);
        scr_calib_start(calib_done);
    } else {
        release_hold();
    }
}

void ui_start_touch_calibration(void)
{
    if (s_cur == SCR_CALIB || !g_platform || !g_platform->touch_raw) return;
    if (!s_holding) {
        s_after_hold = s_cur == SCR_BOOT ? SCR_HOME : s_cur;
        s_holding = true;
    }
    ui_go(SCR_CALIB);
    scr_calib_start(calib_done);
}

void ui_init(void)
{
    s_model = calloc(1, sizeof(dk_model_t));
    g_model = s_model;
    dk_theme_init();
    s_scr[SCR_HOME] = scr_home_create();
    s_scr[SCR_SESSIONS] = scr_sessions_create();
    s_scr[SCR_CHAT] = scr_chat_create();
    s_scr[SCR_ALERT] = scr_alert_create();
    s_scr[SCR_NIGHT] = scr_night_create();
    s_scr[SCR_SETUP] = scr_setup_create();
    s_scr[SCR_SETTINGS] = scr_settings_create();
    s_scr[SCR_BOOT] = scr_boot_create();
    s_scr[SCR_CALIB] = scr_calib_create();
    s_scr[SCR_UPDATE] = scr_update_create(update_back);
    lv_timer_create(tick_cb, 1000, NULL);
    ui_gestures_init();

    s_cur = SCR_BOOT;
    s_holding = true;
    lv_screen_load(s_scr[SCR_BOOT]);
    ui_gestures_on_screen(SCR_BOOT);
    scr_boot_play(boot_done);
}

void ui_set_backlight_cb(ui_backlight_fn fn)
{
    s_backlight = fn;
}

void ui_set_connection(dk_conn_t c, const char *detail)
{
    g_conn = c;
    snprintf(g_conn_detail, sizeof(g_conn_detail), "%s", detail ? detail : "");
    scr_home_refresh(DK_CAUSE_HELLO, "");
}

static const dk_reminder_t *find_reminder(const char *id)
{
    for (int i = 0; i < g_model->reminder_n; i++) {
        if (strcmp(g_model->reminders[i].id, id) == 0) return &g_model->reminders[i];
    }
    return NULL;
}

void ui_apply_model(const dk_model_t *m)
{
    memcpy(s_model, m, sizeof(*m));
    if (m->now) s_relay_offset = m->now - ui_now_ms();

    // Only the screen on show is brought up to date now; the others catch up when they're
    // opened (ui_go refreshes them), so a snapshot doesn't rebuild five screens at once.
    bool alert = scr_alert_refresh();
    if (s_cur == SCR_HOME) scr_home_refresh(m->cause, m->cause_id);
    else if (s_cur == SCR_SESSIONS) scr_sessions_refresh();
    else if (s_cur == SCR_CHAT) scr_chat_refresh(m->cause);
    else if (s_cur == SCR_NIGHT) scr_night_refresh();
    ui_gestures_refresh();

    if (s_cur == SCR_SETUP) return; // finish setting up first
    if (alert && s_cur != SCR_ALERT) {
        set_backlight(100);
        ui_go(SCR_ALERT);
    } else if (!alert && s_cur == SCR_ALERT) {
        ui_go(s_before_alert == SCR_NIGHT ? SCR_HOME : s_before_alert);
    }

    switch (m->cause) {
    case DK_CAUSE_PERMISSION:
        ui_sound(DK_SND_PERMISSION);
        break;
    case DK_CAUSE_QUESTION:
        ui_sound(DK_SND_QUESTION);
        break;
    case DK_CAUSE_REMINDER_DONE:
        ui_sound(DK_SND_CHEER);
        break;
    case DK_CAUSE_CHAT: {
        // Only a reply from the agent, not the echo of what you just sent.
        const dk_agent_t *a = dk_model_find_agent(m, m->cause_id);
        if (a && a->chat_n > 0 && !a->chat[a->chat_n - 1].from_you && !a->chat[a->chat_n - 1].step) ui_sound(DK_SND_MESSAGE);
        break;
    }
    case DK_CAUSE_REMINDER_DUE: {
        const dk_reminder_t *r = find_reminder(m->cause_id);
        if (s_cur == SCR_NIGHT) ui_go(SCR_HOME);
        set_backlight(100);
        ui_sound(DK_SND_REMINDER);
        ui_banner("Reminder", r ? r->title : "", DK_BUTTER);
        break;
    }
    case DK_CAUSE_PING: {
        const dk_agent_t *a = dk_model_find_agent(m, m->cause_id);
        set_backlight(100);
        ui_sound(DK_SND_DONE);
        ui_banner("Claude finished!", a ? a->name : "", DK_GOOD_BG);
        break;
    }
    case DK_CAUSE_FEED: {
        // A new notification: drop a banner in wherever you are (tap it for the full list),
        // unless something more important has the screen or the list is already open.
        const dk_feed_t *f = NULL;
        for (int i = 0; i < m->feed_n && !f; i++)
            if (!m->cause_id[0] || strcmp(m->feed[i].id, m->cause_id) == 0) f = &m->feed[i];
        bool busy = s_cur == SCR_ALERT || s_cur == SCR_SETUP || s_cur == SCR_BOOT || s_cur == SCR_CALIB || s_cur == SCR_UPDATE;
        if (f && !busy && !ui_gestures_panel_open()) {
            ui_sound(DK_SND_MESSAGE);
            const char *icon = f->level == DK_FEED_ALERT ? LV_SYMBOL_WARNING : f->level == DK_FEED_GOOD ? LV_SYMBOL_OK : LV_SYMBOL_BELL;
            lv_color_t bg = f->level == DK_FEED_ALERT ? DK_ALERT_PILL : f->level == DK_FEED_GOOD ? DK_GOOD_BG : DK_LILAC_SOFT;
            banner(f->title, f->body, bg, icon);
        }
        break;
    }
    case DK_CAUSE_DONE:
        ui_sound(DK_SND_DONE);
        if (s_cur != SCR_HOME && s_cur != SCR_NIGHT) {
            const dk_agent_t *a = dk_model_find_agent(m, m->cause_id);
            if (a) ui_banner(a->source == DK_SRC_CURSOR ? "Cursor finished" : "Claude finished", a->name, DK_GOOD_BG);
        }
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------- sound

static bool quiet_hours(void)
{
    time_t now = time(NULL);
    if (now < 1700000000) return false; // clock not set yet
    struct tm tm;
    localtime_r(&now, &tm);
    return tm.tm_hour >= NIGHT_FROM_HOUR || tm.tm_hour < NIGHT_UNTIL_HOUR;
}

void ui_sound(dk_sound_t s)
{
    if (!g_platform || !g_platform->play_sound || s == DK_SND_NONE) return;
    if (g_info.volume == 0) return;
    // Quiet hours: only reminders (you asked for those) may make a sound.
    if (g_info.quiet_night && s != DK_SND_REMINDER && quiet_hours()) return;
    g_platform->play_sound(s);
}

// ------------------------------------------------- device setup & settings

const char *ui_portal_url(void)
{
    static char url[64];
    if (g_info.hostname[0]) snprintf(url, sizeof(url), "http://%s", g_info.hostname);
    else if (g_info.ip[0]) snprintf(url, sizeof(url), "http://%s", g_info.ip);
    else snprintf(url, sizeof(url), "http://192.168.4.1");
    return url;
}

lv_obj_t *ui_qr(lv_obj_t *parent, const char *text, int32_t size)
{
    lv_obj_t *frame = dk_card(parent, lv_color_white(), 20);
    lv_obj_set_style_pad_all(frame, 12, 0);
    lv_obj_t *qr = lv_qrcode_create(frame);
    lv_qrcode_set_size(qr, size);
    lv_qrcode_set_dark_color(qr, DK_INK);
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, text, (uint32_t)strlen(text));
    return frame;
}

void ui_set_platform(const ui_platform_t *p)
{
    g_platform = p;
}

void ui_set_device_info(const ui_device_info_t *info)
{
    bool first = !s_have_info;
    bool was_ready = s_have_info && g_info.wifi_configured && g_info.relay_configured;
    g_info = *info;
    s_have_info = true;
    bool ready = g_info.wifi_configured && g_info.relay_configured;
    scr_setup_refresh();
    scr_settings_refresh();
    if (!ready && (first || s_cur != SCR_SETTINGS) && s_cur != SCR_SETUP) {
        ui_go(SCR_SETUP);
    } else if (ready && s_cur == SCR_SETUP && !was_ready) {
        ui_go(SCR_HOME);
        ui_sound(DK_SND_HELLO);
        ui_banner("All set!", "I'm online and listening for your agents.", DK_GOOD_BG);
    }
}

void ui_wifi_scan_done(const ui_wifi_ap_t *aps, int n)
{
    scr_setup_scan_done(aps, n);
}

void ui_wifi_connect_result(bool ok, const char *message)
{
    scr_setup_connect_result(ok, message);
}

void ui_update_status(ui_update_state_t state, int progress, const char *latest, const char *message)
{
    g_update_state = state;
    g_update_progress = progress;
    if (latest) snprintf(g_update_latest, sizeof(g_update_latest), "%s", latest);
    snprintf(g_update_message, sizeof(g_update_message), "%s", message ? message : "");
    scr_settings_refresh();
    // Installing takes over the screen; anything else that wants it waits until it's over.
    if (state == UI_UPDATE_DOWNLOADING || state == UI_UPDATE_DONE || (state == UI_UPDATE_FAILED && s_cur == SCR_UPDATE)) {
        if (s_cur != SCR_UPDATE && state != UI_UPDATE_FAILED) {
            if (!s_holding) {
                s_after_hold = (s_cur == SCR_BOOT || s_cur == SCR_CALIB) ? SCR_HOME : s_cur;
                s_holding = true;
            }
            scr_update_refresh();
            ui_go(SCR_UPDATE);
        } else {
            scr_update_refresh();
        }
    }
    if (state == UI_UPDATE_AVAILABLE && s_cur == SCR_HOME) {
        char body[96];
        snprintf(body, sizeof(body), "Version %s is ready. Install it from Settings.", g_update_latest);
        ui_banner("Update available", body, DK_LILAC_SOFT);
    }
}
