// iPad-style edge gestures, drawn on lv_layer_top() so they work over every screen:
//   swipe right from the left edge  → back
//   swipe up from the bottom edge   → home (keep holding → sessions)
//   swipe down from the top edge    → notifications (swipe up or tap outside to close)
// Permission requests, questions and first-run setup don't take gestures, so nothing gets
// dismissed by accident.
//
// LVGL doesn't send drag events to the input device itself, so a 10 ms timer watches the
// pointer. Once a drag clearly is a gesture, the press on whatever widget is underneath is
// cancelled, so starting a swipe on a button never clicks it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_internal.h"

#define EDGE 36           // px from an edge where a gesture can start
#define ACTIVATE 24       // px of travel before a drag counts as a gesture
#define COMMIT_BACK 120   // px right to go back
#define COMMIT_HOME 90    // px up to go home
#define HOLD_DIST 150     // px up before "keep holding" can open sessions
#define HOLD_MS 450       // ms holding still to open sessions
#define COMMIT_PANEL 120  // px down to open notifications
#define PANEL_W 820
#define PANEL_H 600
#define PILL_W 150

typedef enum { G_NONE, G_LEFT, G_BOTTOM, G_TOP, G_PANEL } gkind_t;

static struct {
    lv_indev_t *indev;
    bool down, active, held;
    gkind_t kind;
    lv_point_t p0, still_at;
    uint32_t still_since;
    lv_obj_t *pill, *back, *back_icon, *shade, *panel, *list;
    bool panel_open;
    int32_t panel_y;
} g;

// ------------------------------------------------------------ helpers

static bool allowed(gkind_t k)
{
    scr_id_t s = ui_current();
    if (s == SCR_ALERT || s == SCR_SETUP || s == SCR_BOOT || s == SCR_CALIB) return false;
    if (k == G_LEFT) return s == SCR_CHAT || s == SCR_SESSIONS || s == SCR_SETTINGS || s == SCR_NIGHT;
    return true;
}

static void set_opa(void *o, int32_t v)
{
    lv_obj_set_style_opa(o, (lv_opa_t)v, 0);
}

static void set_y(void *o, int32_t v)
{
    lv_obj_set_y(o, v);
    if (o == g.panel) g.panel_y = v;
}

static void set_translate_y(void *o, int32_t v)
{
    lv_obj_set_style_translate_y(o, v, 0);
}

static void anim(lv_obj_t *o, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms, lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, path ? path : lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/** Stop the press on whatever is under the finger; the rest of this touch belongs to the gesture. */
static void take_over(void)
{
    g.active = true;
    lv_indev_reset(g.indev, NULL);
    lv_indev_wait_release(g.indev);
}

// ------------------------------------------------------- notifications

static const char *feed_icon(dk_level_t l)
{
    switch (l) {
    case DK_FEED_ALERT: return LV_SYMBOL_WARNING;
    case DK_FEED_GOOD: return LV_SYMBOL_OK;
    default: return LV_SYMBOL_BELL;
    }
}

static lv_color_t feed_tint(dk_level_t l)
{
    switch (l) {
    case DK_FEED_ALERT: return DK_ALERT_PILL;
    case DK_FEED_GOOD: return DK_GOOD_BG;
    case DK_FEED_REMINDER: return DK_BUTTER;
    default: return DK_LILAC_SOFT;
    }
}

static void row(lv_obj_t *parent, lv_color_t tint, const char *icon, const char *title, const char *body, const char *when)
{
    lv_obj_t *r = dk_card(parent, DK_CARD, 22);
    lv_obj_set_width(r, lv_pct(100));
    lv_obj_set_height(r, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(r, 12, 0);
    dk_row(r, 14);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *ic = dk_card(r, tint, 14);
    lv_obj_set_size(ic, 42, 42);
    lv_obj_set_style_pad_all(ic, 0, 0);
    lv_obj_center(dk_label(ic, &lv_font_montserrat_20, DK_INK, icon));
    lv_obj_t *txt = dk_box(r);
    dk_col(txt, 0);
    lv_obj_set_flex_grow(txt, 1);
    lv_obj_set_width(txt, 0);
    lv_obj_t *t = dk_label(txt, DK_FONT_M, DK_INK, title);
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    if (body && body[0]) {
        lv_obj_t *b = dk_label(txt, DK_FONT_XS, DK_MUTED, body);
        lv_obj_set_width(b, lv_pct(100));
        lv_label_set_long_mode(b, LV_LABEL_LONG_MODE_DOTS);
    }
    if (when && when[0]) dk_label(r, DK_FONT_XS, DK_MUTED, when);
}

static void build_list(void)
{
    if (!g.list) return;
    lv_obj_clean(g.list);
    int shown = 0;
    // Upcoming reminders first: they're the things you can act on.
    for (int i = 0; i < g_model->reminder_n && shown < 3; i++) {
        const dk_reminder_t *r = &g_model->reminders[i];
        if (r->done) continue;
        char when[40] = "";
        if (r->due) ui_due_text(r->due, when, sizeof(when));
        row(g.list, DK_BUTTER, LV_SYMBOL_BELL, r->title, "Reminder", when);
        shown++;
    }
    for (int i = 0; i < g_model->feed_n && shown < 7; i++) {
        const dk_feed_t *f = &g_model->feed[i];
        char ago[12];
        ui_ago(f->ts, ago, sizeof(ago));
        row(g.list, feed_tint(f->level), feed_icon(f->level), f->title, f->body, ago);
        shown++;
    }
    if (!shown) dk_label(g.list, DK_FONT_M, DK_MUTED, "Nothing new. Deskling will let you know.");
}

static void close_panel(void);

static void shade_click(lv_event_t *e)
{
    (void)e;
    close_panel();
}

static void ensure_panel(void)
{
    if (g.panel) return;
    lv_obj_t *top = lv_layer_top();
    g.shade = dk_box(top);
    lv_obj_set_size(g.shade, DK_W, DK_H);
    lv_obj_set_style_bg_color(g.shade, DK_INK, 0);
    lv_obj_set_style_bg_opa(g.shade, LV_OPA_40, 0);
    lv_obj_set_style_opa(g.shade, 0, 0);
    lv_obj_add_flag(g.shade, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g.shade, shade_click, LV_EVENT_CLICKED, NULL);

    g.panel = dk_card(top, DK_GROUND, 36);
    lv_obj_set_size(g.panel, PANEL_W, PANEL_H);
    lv_obj_set_x(g.panel, (DK_W - PANEL_W) / 2);
    set_y(g.panel, -PANEL_H - 20);
    lv_obj_set_style_pad_hor(g.panel, 26, 0);
    lv_obj_set_style_pad_top(g.panel, 30, 0);
    lv_obj_set_style_pad_bottom(g.panel, 18, 0);
    lv_obj_set_style_shadow_width(g.panel, 40, 0);
    lv_obj_set_style_shadow_opa(g.panel, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(g.panel, DK_INK, 0);
    lv_obj_add_flag(g.panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN); // swallow taps so they don't close it
    dk_col(g.panel, 12);

    lv_obj_t *head = dk_box(g.panel);
    lv_obj_set_size(head, lv_pct(100), LV_SIZE_CONTENT);
    dk_row(head, 0);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    dk_label(head, DK_FONT_H2, DK_INK, "Notifications");
    dk_label(head, DK_FONT_S, DK_MUTED, "Swipe up to close");

    g.list = dk_box(g.panel);
    lv_obj_set_width(g.list, lv_pct(100));
    lv_obj_set_flex_grow(g.list, 1);
    dk_col(g.list, 10);
    lv_obj_set_style_clip_corner(g.list, true, 0);

    // A little grabber at the bottom, like a sheet.
    lv_obj_t *grab = dk_card(g.panel, DK_LINE, LV_RADIUS_CIRCLE);
    lv_obj_set_size(grab, 90, 6);
    lv_obj_set_style_bg_color(grab, DK_MUTED, 0);
    lv_obj_set_style_bg_opa(grab, LV_OPA_30, 0);
    lv_obj_add_flag(grab, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(grab, LV_ALIGN_BOTTOM_MID, 0, 6);
}

static void show_panel_at(int32_t y)
{
    ensure_panel();
    lv_obj_remove_flag(g.shade, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g.panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g.shade);
    lv_obj_move_foreground(g.panel);
    set_y(g.panel, y);
    int32_t shown = PANEL_H + y; // 0..PANEL_H
    if (shown < 0) shown = 0;
    lv_obj_set_style_opa(g.shade, (lv_opa_t)(shown * 255 / PANEL_H), 0);
}

static void panel_hidden(lv_anim_t *a)
{
    (void)a;
    lv_obj_add_flag(g.shade, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g.panel, LV_OBJ_FLAG_HIDDEN);
}

static void open_panel(void)
{
    show_panel_at(g.panel_y);
    g.panel_open = true;
    anim(g.panel, set_y, g.panel_y, 0, 320, lv_anim_path_overshoot);
    anim(g.shade, set_opa, lv_obj_get_style_opa(g.shade, 0), 255, 280, NULL);
}

static void close_panel(void)
{
    if (!g.panel) return;
    g.panel_open = false;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g.panel);
    lv_anim_set_exec_cb(&a, set_y);
    lv_anim_set_values(&a, g.panel_y, -PANEL_H - 20);
    lv_anim_set_duration(&a, 260);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, panel_hidden);
    lv_anim_start(&a);
    anim(g.shade, set_opa, lv_obj_get_style_opa(g.shade, 0), 0, 260, NULL);
}

// ---------------------------------------------------------- back bubble

static void back_hidden(lv_anim_t *a)
{
    (void)a;
    lv_obj_add_flag(g.back, LV_OBJ_FLAG_HIDDEN);
}

static void show_back(int32_t dx, int32_t y)
{
    if (dx > 150) dx = 150;
    bool ready = dx >= COMMIT_BACK;
    lv_obj_remove_flag(g.back, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g.back);
    lv_obj_set_pos(g.back, dx - 72, y - 32);
    lv_obj_set_style_opa(g.back, (lv_opa_t)LV_MIN(255, dx * 255 / COMMIT_BACK), 0);
    lv_obj_set_style_bg_color(g.back, ready ? DK_INK : DK_CARD, 0);
    lv_obj_set_style_text_color(g.back_icon, ready ? DK_GROUND : DK_INK, 0);
}

static void hide_back(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g.back);
    lv_anim_set_exec_cb(&a, set_opa);
    lv_anim_set_values(&a, lv_obj_get_style_opa(g.back, 0), 0);
    lv_anim_set_duration(&a, 180);
    lv_anim_set_completed_cb(&a, back_hidden);
    lv_anim_start(&a);
}

// ------------------------------------------------------------- pointer

static void on_down(lv_point_t p)
{
    g.down = true;
    g.active = g.held = false;
    g.p0 = g.still_at = p;
    g.still_since = lv_tick_get();
    if (g.panel_open) g.kind = p.y < PANEL_H ? G_PANEL : G_NONE; // below the sheet is a tap on the shade
    else if (p.x < EDGE && allowed(G_LEFT)) g.kind = G_LEFT;
    else if (p.y > DK_H - EDGE && allowed(G_BOTTOM)) g.kind = G_BOTTOM;
    else if (p.y < EDGE && allowed(G_TOP)) g.kind = G_TOP;
    else g.kind = G_NONE;
}

static void on_move(lv_point_t p)
{
    int32_t dx = p.x - g.p0.x, dy = p.y - g.p0.y;
    switch (g.kind) {
    case G_LEFT:
        if (!g.active && dx > ACTIVATE && dx > LV_ABS(dy)) take_over();
        if (g.active) show_back(dx, p.y);
        break;
    case G_BOTTOM: {
        int32_t up = -dy;
        if (!g.active && up > ACTIVATE && up > LV_ABS(dx)) take_over();
        if (!g.active || g.held) break;
        lv_obj_set_style_translate_y(g.pill, -LV_MIN(up, 240) / 3, 0);
        lv_obj_set_width(g.pill, PILL_W + LV_MIN(up, 240) / 2);
        lv_obj_set_x(g.pill, (DK_W - lv_obj_get_width(g.pill)) / 2);
        // Keep holding high enough and still enough → sessions, like the iPad app switcher.
        if (LV_ABS(p.x - g.still_at.x) > 12 || LV_ABS(p.y - g.still_at.y) > 12) {
            g.still_at = p;
            g.still_since = lv_tick_get();
        }
        bool high = up > HOLD_DIST;
        lv_obj_set_style_bg_color(g.pill, high ? DK_PURPLE : (ui_current() == SCR_NIGHT ? DK_NIGHT_TEXT : DK_INK), 0);
        if (high && lv_tick_elaps(g.still_since) > HOLD_MS) {
            g.held = true;
            ui_sound(DK_SND_MESSAGE);
            ui_go(SCR_SESSIONS);
        }
        break;
    }
    case G_TOP:
        if (!g.active && dy > ACTIVATE && dy > LV_ABS(dx)) {
            take_over();
            ensure_panel();
            build_list();
        }
        if (g.active) show_panel_at(-PANEL_H + LV_CLAMP(0, dy, PANEL_H));
        break;
    case G_PANEL:
        if (!g.active && -dy > ACTIVATE) take_over();
        if (g.active) show_panel_at(LV_MIN(0, dy));
        break;
    default:
        break;
    }
}

static void pill_rest(void)
{
    anim(g.pill, set_translate_y, lv_obj_get_style_translate_y(g.pill, 0), 0, 260, lv_anim_path_overshoot);
    lv_obj_set_width(g.pill, PILL_W);
    lv_obj_set_x(g.pill, (DK_W - PILL_W) / 2);
    ui_gestures_on_screen(ui_current());
}

static void on_up(lv_point_t p)
{
    int32_t dx = p.x - g.p0.x, dy = p.y - g.p0.y;
    if (g.active) {
        switch (g.kind) {
        case G_LEFT:
            hide_back();
            if (dx >= COMMIT_BACK && ui_back()) ui_sound(DK_SND_TAP);
            break;
        case G_BOTTOM:
            pill_rest();
            if (!g.held && -dy >= COMMIT_HOME && ui_current() != SCR_HOME) ui_go(SCR_HOME);
            break;
        case G_TOP:
            if (dy >= COMMIT_PANEL) {
                ui_sound(DK_SND_TAP);
                open_panel();
            } else {
                close_panel();
            }
            break;
        case G_PANEL:
            if (-dy >= 80) close_panel();
            else open_panel();
            break;
        default:
            break;
        }
    }
    g.down = g.active = false;
    g.kind = G_NONE;
}

static void poll(lv_timer_t *t)
{
    (void)t;
    if (!g.indev) {
        for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i)) {
            if (lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER) g.indev = i;
        }
        if (!g.indev) return;
    }
    lv_point_t p;
    lv_indev_get_point(g.indev, &p);
    bool pressed = lv_indev_get_state(g.indev) == LV_INDEV_STATE_PRESSED;
    if (pressed && !g.down) on_down(p);
    else if (pressed) on_move(p);
    else if (g.down) on_up(p);
}

// ------------------------------------------------------------------ api

void ui_gestures_init(void)
{
    lv_obj_t *top = lv_layer_top();

    g.pill = dk_card(top, DK_INK, LV_RADIUS_CIRCLE);
    lv_obj_set_size(g.pill, PILL_W, 6);
    lv_obj_set_pos(g.pill, (DK_W - PILL_W) / 2, DK_H - 14);
    lv_obj_set_style_bg_opa(g.pill, LV_OPA_30, 0);
    lv_obj_remove_flag(g.pill, LV_OBJ_FLAG_CLICKABLE);

    g.back = dk_card(top, DK_CARD, LV_RADIUS_CIRCLE);
    lv_obj_set_size(g.back, 64, 64);
    lv_obj_set_style_pad_all(g.back, 0, 0);
    lv_obj_set_style_shadow_width(g.back, 18, 0);
    lv_obj_set_style_shadow_opa(g.back, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(g.back, DK_INK, 0);
    lv_obj_remove_flag(g.back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g.back, LV_OBJ_FLAG_HIDDEN);
    g.back_icon = dk_label(g.back, &lv_font_montserrat_28, DK_INK, LV_SYMBOL_LEFT);
    lv_obj_center(g.back_icon);

    lv_timer_create(poll, 10, NULL);
}

void ui_gestures_on_screen(scr_id_t id)
{
    if (!g.pill) return;
    bool show = id != SCR_SETUP && id != SCR_ALERT && id != SCR_BOOT && id != SCR_CALIB;
    if (show) lv_obj_remove_flag(g.pill, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g.pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(g.pill, id == SCR_NIGHT ? DK_NIGHT_TEXT : DK_INK, 0);
    if (!show && g.panel_open) close_panel();
}

void ui_gestures_refresh(void)
{
    if (g.panel_open || (g.active && g.kind == G_TOP)) build_list();
}

bool ui_gestures_panel_open(void)
{
    return g.panel_open;
}
