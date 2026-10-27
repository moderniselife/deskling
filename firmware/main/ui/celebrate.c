// Celebrations for the game (quests): every new event from the relay plays once, one after
// another, on the top layer so they show over whatever screen you're on. Each is built from a
// few small boxes moved by translate and opacity (cheap to draw), and cleaned up when it ends.
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define QUEUE_MAX 8

static dk_game_event_t s_queue[QUEUE_MAX];
static int s_queue_n;
static uint32_t s_seen;  // the newest event already played (or skipped)
static bool s_baseline;  // we've seen a score at least once since boot
static lv_obj_t *s_now;  // the celebration on screen
static lv_timer_t *s_next;
static uint32_t s_played; // for tests

uint32_t ui_celebrations_played(void) { return s_played; }

// -------------------------------------------------------------- motion bits

static void ex_tx(void *o, int32_t v) { dk_style(o, LV_STYLE_TRANSLATE_X, v); }
static void ex_ty(void *o, int32_t v) { dk_style(o, LV_STYLE_TRANSLATE_Y, v); }
static void ex_opa(void *o, int32_t v) { dk_style(o, LV_STYLE_OPA, v); }
static void ex_bg_opa(void *o, int32_t v) { dk_style(o, LV_STYLE_BG_OPA, v); }
static void ex_size(void *o, int32_t v)
{
    lv_obj_set_size(o, v, v);
    lv_obj_set_style_translate_x(o, -v / 2, 0);
    lv_obj_set_style_translate_y(o, -v / 2, 0);
}

static void go(lv_obj_t *o, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t dur, uint32_t delay, lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, dur);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, path ? path : lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static lv_obj_t *box(lv_obj_t *p, int32_t w, int32_t h, lv_color_t c, int32_t r)
{
    lv_obj_t *o = dk_box(p);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

/* A burst of little squares flying out from (x, y) and fading. */
static void burst(lv_obj_t *p, int32_t x, int32_t y, int n, int32_t reach, uint32_t delay)
{
    lv_color_t c[4] = {DK_SPARK, DK_PEACH, DK_LILAC, DK_GOOD_INK};
    // Directions round a circle, from a small table (no trig at runtime).
    static const int8_t dir[12][2] = {{100, 0}, {87, 50}, {50, 87}, {0, 100}, {-50, 87}, {-87, 50}, {-100, 0}, {-87, -50}, {-50, -87}, {0, -100}, {50, -87}, {87, -50}};
    for (int i = 0; i < n; i++) {
        int32_t sz = 10 + (i % 3) * 4;
        lv_obj_t *s = box(p, sz, sz, c[i % 4], i % 2 ? 3 : LV_RADIUS_CIRCLE);
        lv_obj_set_pos(s, x - sz / 2, y - sz / 2);
        const int8_t *d = dir[(i * 12 / n) % 12];
        int32_t r = reach * (80 + (i * 37) % 40) / 100;
        go(s, ex_tx, 0, d[0] * r / 100, 700, delay, lv_anim_path_ease_out);
        go(s, ex_ty, 0, d[1] * r / 100, 700, delay, lv_anim_path_ease_out);
        go(s, ex_opa, LV_OPA_COVER, LV_OPA_TRANSP, 450, delay + 350, lv_anim_path_linear);
    }
}

/* Confetti falling across the whole screen. */
static void confetti(lv_obj_t *p, int n, uint32_t delay)
{
    lv_color_t c[5] = {DK_SPARK, DK_PEACH, DK_LILAC, DK_GOOD_INK, DK_PURPLE};
    for (int i = 0; i < n; i++) {
        lv_obj_t *s = box(p, 10 + (i % 3) * 4, 16 + (i % 2) * 6, c[i % 5], 3);
        int32_t x = 40 + (i * 1213) % (DK_W - 80);
        lv_obj_set_pos(s, x, -30);
        uint32_t d = delay + (uint32_t)((i * 97) % 600);
        go(s, ex_ty, 0, DK_H + 60, 1800 + (i % 4) * 250, d, lv_anim_path_ease_in);
        go(s, ex_tx, 0, ((i % 5) - 2) * 40, 1800, d, lv_anim_path_ease_in_out);
    }
}

/* Rings growing out from a point (their own size, not a scale). */
static void rings(lv_obj_t *p, int32_t x, int32_t y, lv_color_t c, int n)
{
    for (int i = 0; i < n; i++) {
        lv_obj_t *r = dk_box(p);
        lv_obj_set_pos(r, x, y);
        lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(r, 6, 0);
        lv_obj_set_style_border_color(r, c, 0);
        go(r, ex_size, 40, 460, 1000, (uint32_t)i * 280, lv_anim_path_ease_out);
        go(r, ex_opa, LV_OPA_COVER, LV_OPA_TRANSP, 1000, (uint32_t)i * 280, lv_anim_path_linear);
    }
}

// --------------------------------------------------------------- the shows

static void finished(lv_timer_t *t);

static void end_after(uint32_t ms)
{
    s_next = lv_timer_create(finished, ms, NULL);
    lv_timer_set_repeat_count(s_next, 1);
}

static void tap_away(lv_event_t *e)
{
    (void)e;
    if (s_next) {
        lv_timer_delete(s_next);
        s_next = NULL;
    }
    finished(NULL);
}

static lv_obj_t *stage(bool dim)
{
    lv_obj_t *o = dk_box(lv_layer_top());
    lv_obj_set_size(o, DK_W, DK_H);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    if (dim) {
        lv_obj_set_style_bg_color(o, DK_INK, 0);
        go(o, ex_bg_opa, 0, 150, 260, 0, NULL);
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(o, tap_away, LV_EVENT_CLICKED, NULL);
    } else {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE); // touches go through to the screen
    }
    return o;
}

/* A pill that floats up from the bottom and fades: "+44 XP · DKL-31". */
static void float_pill(lv_obj_t *st, const char *text, lv_color_t bg, lv_color_t ink, int32_t y0)
{
    lv_obj_t *p = dk_pill(st, bg, DK_FONT_XL, text);
    lv_obj_set_style_text_color(p, ink, 0);
    lv_obj_set_style_pad_hor(p, 26, 0);
    lv_obj_set_style_pad_ver(p, 12, 0);
    lv_obj_set_style_shadow_width(p, 24, 0);
    lv_obj_set_style_shadow_opa(p, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(p, DK_INK, 0);
    lv_obj_align(p, LV_ALIGN_TOP_MID, 0, y0);
    dk_anim_pop(p, 0);
    go(p, ex_ty, 0, -90, 1300, 250, lv_anim_path_ease_out);
    go(p, ex_opa, LV_OPA_COVER, LV_OPA_TRANSP, 400, 1150, lv_anim_path_linear);
    burst(st, DK_W / 2, y0 + 28, 10, 150, 80);
}

/* A big card in the middle of the screen. */
static lv_obj_t *card(lv_obj_t *st, lv_color_t bg, const char *title, const char *body, const lv_font_t *tf)
{
    lv_obj_t *c = dk_card(st, bg, 36);
    lv_obj_set_style_pad_hor(c, 48, 0);
    lv_obj_set_style_pad_ver(c, 30, 0);
    dk_col(c, 8);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *t = dk_label(c, tf, DK_INK, title);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    if (body && body[0]) {
        lv_obj_t *b = dk_label(c, DK_FONT_L, DK_INK_SOFT, body);
        lv_obj_set_style_max_width(b, 760, 0);
        lv_label_set_long_mode(b, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_center(c);
    dk_anim_pop(c, 60);
    return c;
}

static void play(const dk_game_event_t *e)
{
    char t[96], b[96];
    s_played++;
    switch (e->kind) {
    case DK_EV_XP:
    case DK_EV_FOCUS:
        s_now = stage(false);
        snprintf(t, sizeof(t), "+%d XP  %s", (int)e->amount, e->label);
        float_pill(s_now, t, DK_CARD, DK_PURPLE, DK_H - 210);
        ui_sound(DK_SND_CHEER);
        end_after(1700);
        break;
    case DK_EV_COMBO: {
        s_now = stage(false);
        snprintf(t, sizeof(t), "Combo x%d!", (int)e->amount);
        lv_obj_t *p = dk_pill(s_now, DK_BUTTER, DK_FONT_H2, t);
        lv_obj_set_style_pad_hor(p, 34, 0);
        lv_obj_set_style_pad_ver(p, 14, 0);
        lv_obj_align(p, LV_ALIGN_TOP_MID, 0, 150);
        dk_anim_pop(p, 0);
        dk_anim_shake(p);
        go(p, ex_opa, LV_OPA_COVER, LV_OPA_TRANSP, 350, 1200, lv_anim_path_linear);
        burst(s_now, DK_W / 2, 185, 12, 200, 120);
        end_after(1650);
        break;
    }
    case DK_EV_BOSS: {
        s_now = stage(true);
        rings(s_now, DK_W / 2, DK_H / 2, DK_ALERT_RING, 2);
        snprintf(b, sizeof(b), "%s  ·  +%d XP", e->label, (int)e->amount);
        card(s_now, DK_ALERT_BG, "Boss defeated!", b, DK_FONT_H1);
        burst(s_now, DK_W / 2, DK_H / 2, 12, 420, 100);
        burst(s_now, DK_W / 2, DK_H / 2, 12, 260, 380);
        ui_sound(DK_SND_DONE);
        end_after(2600);
        break;
    }
    case DK_EV_LEVEL:
    case DK_EV_UNLOCK: {
        s_now = stage(true);
        rings(s_now, DK_W / 2, DK_H / 2 - 40, DK_LILAC, 2);
        confetti(s_now, 26, 200);
        lv_obj_t *c = card(s_now, DK_LILAC, e->kind == DK_EV_LEVEL ? e->label : "Unlocked!", e->kind == DK_EV_UNLOCK ? e->label : "Deskling levelled up", DK_FONT_H1);
        lv_obj_t *face = deskling_create(c, 150, DK_FACE);
        lv_obj_move_to_index(face, 0);
        deskling_set_outfit(face, g_model->quests.outfits);
        deskling_set_mood(face, DK_MOOD_HAPPY);
        ui_sound(DK_SND_HELLO);
        end_after(3400);
        break;
    }
    case DK_EV_CLEARED:
        s_now = stage(true);
        confetti(s_now, 32, 0);
        snprintf(b, sizeof(b), "Every quest for today is done  ·  +%d XP", (int)e->amount);
        card(s_now, DK_GOOD_BG, "Day cleared!", b, DK_FONT_H1);
        ui_sound(DK_SND_CHEER);
        end_after(3000);
        break;
    case DK_EV_CHALLENGE:
        s_now = stage(true);
        snprintf(b, sizeof(b), "%s  ·  +%d XP", e->label, (int)e->amount);
        card(s_now, DK_BUTTER_BG, "Weekly challenge complete", b, DK_FONT_H2);
        burst(s_now, DK_W / 2, DK_H / 2, 12, 380, 120);
        ui_sound(DK_SND_CHEER);
        end_after(2400);
        break;
    case DK_EV_STREAK:
    default:
        s_now = stage(false);
        snprintf(t, sizeof(t), "%s", e->label);
        float_pill(s_now, t, DK_ALERT_PILL, DK_ALERT_INK, DK_H - 210);
        end_after(1600);
        break;
    }
}

static bool can_play(void)
{
    scr_id_t s = ui_current();
    return s == SCR_HOME || s == SCR_SESSIONS || s == SCR_CHAT || s == SCR_QUESTS || s == SCR_SETTINGS;
}

static void next(void)
{
    if (s_now || !s_queue_n || !can_play()) return;
    dk_game_event_t e = s_queue[0];
    memmove(&s_queue[0], &s_queue[1], sizeof(s_queue[0]) * (size_t)(s_queue_n - 1));
    s_queue_n--;
    // A level-up and its unlock play as one: show the unlock on the level card.
    if (e.kind == DK_EV_LEVEL && s_queue_n && s_queue[0].kind == DK_EV_UNLOCK) {
        dk_game_event_t u = s_queue[0];
        memmove(&s_queue[0], &s_queue[1], sizeof(s_queue[0]) * (size_t)(s_queue_n - 1));
        s_queue_n--;
        snprintf(e.label + strlen(e.label), sizeof(e.label) - strlen(e.label), "!");
        e.kind = DK_EV_LEVEL;
        play(&e);
        // Swap the subtitle for what was unlocked.
        lv_obj_t *c = lv_obj_get_child(s_now, -1);
        lv_obj_t *sub = c ? lv_obj_get_child(c, -1) : NULL;
        if (sub && lv_obj_check_type(sub, &lv_label_class)) lv_label_set_text(sub, u.label);
        return;
    }
    play(&e);
}

static void finished(lv_timer_t *t)
{
    (void)t;
    s_next = NULL;
    if (s_now) {
        lv_anim_delete(s_now, NULL);
        lv_obj_delete(s_now);
        s_now = NULL;
    }
    next();
}

void ui_celebrate_model(const dk_quests_t *q)
{
    if (!q->on) return;
    if (!s_baseline || q->seq < s_seen) {
        // First score since boot (or the relay started over): don't replay old news.
        s_baseline = true;
        s_seen = q->seq;
        return;
    }
    for (int i = 0; i < q->event_n; i++) {
        const dk_game_event_t *e = &q->events[i];
        if (e->seq <= s_seen) continue;
        if (s_queue_n < QUEUE_MAX) s_queue[s_queue_n++] = *e;
        s_seen = e->seq;
    }
    next();
}

/* The screen changed: anything waiting (behind an alert, say) can play now. */
void ui_celebrate_resume(void)
{
    next();
}
