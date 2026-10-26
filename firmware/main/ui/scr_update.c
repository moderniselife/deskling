// Installing an update (over the air, or a .bin uploaded from the setup page): Deskling gets
// busy beside a big rounded progress bar with a shine running along it, the percentage and
// the version it's moving to. When it's done Deskling cheers and the bar turns green before
// the restart; if it fails there's a gentle message and a way back.
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define BAR_W 560
#define BAR_H 36
#define SHINE_W 90

static struct {
    lv_obj_t *scr, *face, *col, *title, *sub, *track, *fill, *shine, *pct, *hint, *back;
    ui_update_state_t shown;
    int32_t fill_w, target_w;
    lv_timer_t *chase;
    void (*on_back)(void);
} u;

static void set_fill(int32_t w)
{
    lv_obj_set_width(u.fill, w);
    u.fill_w = w;
}

/** Ease the fill toward its target a little every frame, however often progress arrives. */
static void chase(lv_timer_t *t)
{
    int32_t d = u.target_w - u.fill_w;
    if (d == 0) {
        lv_timer_pause(t);
        return;
    }
    int32_t step = d / 6;
    if (step == 0) step = d > 0 ? 1 : -1;
    set_fill(u.fill_w + step);
}

static void shine_x(void *o, int32_t v)
{
    // Sweep from just off the left of the filled part to just past its right end.
    dk_style(o, LV_STYLE_TRANSLATE_X, -SHINE_W + (v * (u.fill_w + SHINE_W)) / 1000);
}

static void back_clicked(lv_event_t *e)
{
    (void)e;
    if (u.on_back) u.on_back();
}

lv_obj_t *scr_update_create(void (*on_back)(void))
{
    u.on_back = on_back;
    u.shown = UI_UPDATE_IDLE;
    u.scr = lv_obj_create(NULL);
    lv_obj_remove_flag(u.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(u.scr, DK_GROUND, 0);
    lv_obj_set_style_bg_opa(u.scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(u.scr, 96, 0);
    dk_row(u.scr, 80);
    lv_obj_set_flex_align(u.scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Deskling on its lilac card, like on the home screen.
    lv_obj_t *stage = dk_card(u.scr, DK_LILAC, 64);
    lv_obj_set_size(stage, 360, 360);
    lv_obj_set_style_pad_all(stage, 0, 0);
    dk_overflow(stage, 80);
    u.face = deskling_create(stage, 260, DK_FACE);
    lv_obj_center(u.face);

    u.col = dk_box(u.scr);
    lv_obj_set_width(u.col, BAR_W);
    dk_col(u.col, 14);
    u.title = dk_label(u.col, DK_FONT_H1, DK_INK, "Getting an upgrade");
    u.sub = dk_label(u.col, DK_FONT_L, DK_MUTED, "");

    lv_obj_t *gap = dk_box(u.col);
    lv_obj_set_size(gap, 1, 18);

    u.track = dk_card(u.col, DK_LILAC_SOFT, LV_RADIUS_CIRCLE);
    lv_obj_set_size(u.track, BAR_W, BAR_H);
    lv_obj_set_style_pad_all(u.track, 0, 0);
    lv_obj_set_style_clip_corner(u.track, true, 0);
    u.fill = dk_card(u.track, DK_PURPLE, LV_RADIUS_CIRCLE);
    lv_obj_set_size(u.fill, BAR_H, BAR_H);
    lv_obj_set_style_pad_all(u.fill, 0, 0);
    lv_obj_set_style_clip_corner(u.fill, true, 0);
    u.fill_w = u.target_w = BAR_H;
    u.chase = lv_timer_create(chase, 16, NULL);
    lv_timer_pause(u.chase);
    u.shine = dk_card(u.fill, lv_color_white(), LV_RADIUS_CIRCLE);
    lv_obj_set_size(u.shine, SHINE_W, BAR_H);
    lv_obj_set_style_pad_all(u.shine, 0, 0);
    lv_obj_set_style_bg_opa(u.shine, LV_OPA_30, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, u.shine);
    lv_anim_set_exec_cb(&a, shine_x);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 1300);
    lv_anim_set_repeat_delay(&a, 500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    lv_obj_t *row = dk_box(u.col);
    lv_obj_set_width(row, lv_pct(100));
    dk_row(row, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    u.hint = dk_label(row, DK_FONT_M, DK_MUTED, "Keep me plugged in, I'll restart by myself.");
    u.pct = dk_label(row, DK_FONT_XL, DK_INK, "0%");

    u.back = dk_button(u.col, "Back", true, DK_FONT_L);
    lv_obj_set_width(u.back, 200);
    lv_obj_add_event_cb(u.back, back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(u.back, LV_OBJ_FLAG_HIDDEN);
    return u.scr;
}

static void bar_to(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    u.target_w = BAR_H + ((BAR_W - BAR_H) * pct) / 100;
    if (u.target_w != u.fill_w) lv_timer_resume(u.chase);
}

void scr_update_refresh(void)
{
    ui_update_state_t st = g_update_state;
    bool entering = u.shown != st;
    char buf[96];
    const char *to = g_update_latest[0] ? g_update_latest : NULL;
    switch (st) {
    case UI_UPDATE_DONE:
        if (entering) {
            lv_label_set_text(u.title, "All done!");
            snprintf(buf, sizeof(buf), "Restarting into %s…", to ? to : "the new version");
            lv_label_set_text(u.sub, buf);
            lv_label_set_text(u.hint, "See you in a second.");
            lv_obj_set_style_bg_color(u.fill, DK_GOOD_INK, 0);
            lv_obj_add_flag(u.back, LV_OBJ_FLAG_HIDDEN);
            deskling_celebrate(u.face, 4000);
            ui_sound(DK_SND_DONE);
        }
        bar_to(100);
        lv_label_set_text(u.pct, "100%");
        break;
    case UI_UPDATE_FAILED:
        if (entering) {
            lv_label_set_text(u.title, "Hmm, that didn't work");
            lv_label_set_text(u.sub, g_update_message[0] ? g_update_message : "Nothing changed, so you can try again.");
            lv_label_set_text(u.hint, "Nothing changed. You can try again.");
            lv_obj_set_style_bg_color(u.fill, DK_ALERT_INK, 0);
            lv_obj_remove_flag(u.back, LV_OBJ_FLAG_HIDDEN);
            dk_anim_rise(u.back, 0);
            deskling_set_mood(u.face, DK_MOOD_WORRIED);
            ui_sound(DK_SND_OOPS);
        }
        break;
    default: // downloading / installing
        if (entering) {
            lv_label_set_text(u.title, "Getting an upgrade");
            if (to && g_info.version[0] && strcmp(to, g_info.version) != 0) snprintf(buf, sizeof(buf), "%s  " LV_SYMBOL_RIGHT "  %s", g_info.version, to);
            else snprintf(buf, sizeof(buf), "%s", to ? to : "A fresh version");
            lv_label_set_text(u.sub, buf);
            lv_label_set_text(u.hint, "Keep me plugged in, I'll restart by myself.");
            lv_obj_set_style_bg_color(u.fill, DK_PURPLE, 0);
            lv_obj_add_flag(u.back, LV_OBJ_FLAG_HIDDEN);
            deskling_set_mood(u.face, DK_MOOD_BUSY);
            if (u.shown != UI_UPDATE_DOWNLOADING) {
                set_fill(BAR_H);
                u.target_w = BAR_H;
                dk_anim_rise(u.col, 0);
            }
        }
        bar_to(g_update_progress);
        lv_label_set_text_fmt(u.pct, "%d%%", g_update_progress < 0 ? 0 : g_update_progress > 100 ? 100 : g_update_progress);
        break;
    }
    u.shown = st;
}
