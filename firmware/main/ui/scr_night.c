// Night: a big dim clock, a sleeping Deskling and twinkling stars. Tap anywhere to wake.
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

static struct {
    lv_obj_t *scr, *hh, *mm, *ampm, *date, *status_dot, *status, *next, *overnight, *face;
} n;

static void wake_cb(lv_event_t *e)
{
    (void)e;
    ui_go(SCR_HOME);
}

static void star(lv_obj_t *parent, int32_t x, int32_t y, int32_t size, lv_color_t c, uint32_t delay)
{
    lv_obj_t *s = dk_box(parent);
    lv_obj_add_flag(s, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(s, size, size);
    lv_obj_set_pos(s, x, y);
    lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s, c, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    dk_anim_breathe_opa(s, LV_OPA_20, LV_OPA_COVER, 3000, delay);
}

static lv_obj_t *night_card(lv_obj_t *parent, const char *heading, lv_obj_t **value)
{
    lv_obj_t *c = dk_card(parent, DK_NIGHT_CARD, 28);
    lv_obj_set_style_pad_hor(c, 26, 0);
    lv_obj_set_style_pad_ver(c, 20, 0);
    dk_col(c, 4);
    dk_label(c, DK_FONT_S, DK_NIGHT_MUTED, heading);
    *value = dk_label(c, DK_FONT_XL, DK_NIGHT_TEXT, "");
    lv_obj_set_width(*value, lv_pct(100));
    dk_one_line(*value);
    return c;
}

lv_obj_t *scr_night_create(void)
{
    n.scr = lv_obj_create(NULL);
    lv_obj_remove_flag(n.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(n.scr, DK_NIGHT, 0);
    lv_obj_set_style_pad_hor(n.scr, 64, 0);
    lv_obj_set_style_pad_ver(n.scr, 48, 0);
    dk_col(n.scr, 0);
    lv_obj_set_flex_align(n.scr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(n.scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(n.scr, wake_cb, LV_EVENT_CLICKED, NULL);

    star(n.scr, 180, 150, 6, DK_LILAC, 0);
    star(n.scr, 620, 110, 4, DK_LILAC, 800);
    star(n.scr, 980, 190, 7, DK_BUTTER, 1600);
    star(n.scr, 760, 600, 5, DK_LILAC, 2200);
    star(n.scr, 1180, 520, 5, DK_BUTTER, 1100);
    star(n.scr, 90, 520, 4, DK_LILAC, 400);
    star(n.scr, 420, 330, 3, DK_BUTTER, 1900);

    lv_obj_t *top = dk_box(n.scr);
    lv_obj_set_width(top, lv_pct(100));
    dk_row(top, 8);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    dk_label(top, DK_FONT_L, DK_LILAC, "deskling");
    lv_obj_t *st = dk_box(top);
    dk_row(st, 10);
    n.status_dot = dk_box(st);
    lv_obj_set_size(n.status_dot, 10, 10);
    lv_obj_set_style_radius(n.status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(n.status_dot, DK_NIGHT_GOOD, 0);
    lv_obj_set_style_bg_opa(n.status_dot, LV_OPA_COVER, 0);
    dk_anim_breathe_opa(n.status_dot, LV_OPA_50, LV_OPA_COVER, 3000, 0);
    n.status = dk_label(st, DK_FONT_M, DK_NIGHT_MUTED, "All agents quiet");

    lv_obj_t *mid = dk_box(n.scr);
    lv_obj_set_width(mid, lv_pct(100));
    dk_row(mid, 48);
    lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *clock = dk_box(mid);
    dk_col(clock, 8);
    lv_obj_t *time = dk_box(clock);
    dk_row(time, 0);
    lv_obj_set_flex_align(time, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    n.hh = dk_label(time, DK_FONT_BIGCLOCK, DK_NIGHT_TEXT, "12");
    lv_obj_t *colon = dk_label(time, DK_FONT_BIGCLOCK, DK_NIGHT_TEXT, ":");
    dk_anim_breathe_opa(colon, LV_OPA_30, LV_OPA_COVER, 2000, 0);
    n.mm = dk_label(time, DK_FONT_BIGCLOCK, DK_NIGHT_TEXT, "00");
    n.ampm = dk_label(time, DK_FONT_H1, DK_NIGHT_MUTED, "pm");
    lv_obj_set_style_pad_left(n.ampm, 12, 0);
    lv_obj_set_style_pad_bottom(n.ampm, 30, 0);
    n.date = dk_label(clock, DK_FONT_H2, DK_NIGHT_MUTED, "");
    lv_obj_set_style_pad_left(n.date, 8, 0);

    lv_obj_t *fw = dk_box(mid);
    lv_obj_set_size(fw, 260, 260);
    dk_overflow(fw, 120);
    n.face = deskling_create(fw, 240, DK_LILAC);
    lv_obj_center(n.face);
    deskling_set_mood(n.face, DK_MOOD_SLEEPY);
    dk_anim_bob(fw, 6, 9000, 0);

    lv_obj_t *cards = dk_box(n.scr);
    lv_obj_set_width(cards, lv_pct(100));
    dk_row(cards, 20);
    lv_obj_t *c1 = night_card(cards, "NEXT REMINDER", &n.next);
    lv_obj_set_flex_grow(c1, 1);
    lv_obj_t *c2 = night_card(cards, "OVERNIGHT", &n.overnight);
    lv_obj_set_width(c2, 380);

    scr_night_tick();
    scr_night_refresh();
    return n.scr;
}

void scr_night_refresh(void)
{
    if (!n.scr) return;
    int working = ui_count_state(DK_WORKING);
    char buf[160];
    if (working) snprintf(buf, sizeof(buf), "%d agent%s still working", working, working == 1 ? "" : "s");
    else snprintf(buf, sizeof(buf), "All agents quiet");
    dk_set_text(n.status, buf);
    lv_obj_set_style_bg_color(n.status_dot, working ? DK_LILAC : DK_NIGHT_GOOD, 0);

    const dk_reminder_t *next = NULL;
    for (int i = 0; i < g_model->reminder_n; i++) {
        const dk_reminder_t *r = &g_model->reminders[i];
        if (!r->done && (!next || (r->due && (!next->due || r->due < next->due)))) next = r;
    }
    if (next) {
        char due[40];
        ui_due_text(next->due, due, sizeof(due));
        snprintf(buf, sizeof(buf), "%s · %s", next->title, due);
        dk_set_text(n.next, buf);
    } else {
        dk_set_text(n.next, "Nothing planned. Sleep well!");
    }

    if (working) {
        for (int i = 0; i < g_model->agent_n; i++) {
            if (g_model->agents[i].state == DK_WORKING) {
                snprintf(buf, sizeof(buf), "%s · %s", g_model->agents[i].name, g_model->agents[i].host);
                break;
            }
        }
    } else if (g_model->feed_n) {
        snprintf(buf, sizeof(buf), "%s", g_model->feed[0].title);
    } else {
        snprintf(buf, sizeof(buf), "Nothing scheduled");
    }
    dk_set_text(n.overnight, buf);
}

void scr_night_tick(void)
{
    if (!n.scr) return;
    char hh[4], mm[4], ampm[4], date[48];
    ui_clock_parts(hh, mm, ampm, date, sizeof(date));
    lv_label_set_text(n.hh, hh);
    lv_label_set_text(n.mm, mm);
    lv_label_set_text(n.ampm, ampm);
    lv_label_set_text(n.date, date);
}
