// Touch setup: tap five dots, Deskling fits how the touch sensor lines up with the screen
// (rotation, flips, scale and offset in one affine map), then you check it by tapping Done.
// This screen reads the raw sensor itself, so it works however wrong the old mapping was.
// If Done isn't tapped within 20 s (say the fit came out badly), it starts over.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

const lv_point_t g_calib_targets[CALIB_POINTS] = {{140, 130}, {1140, 130}, {1140, 670}, {140, 670}, {640, 400}};

#define MAX_ERR_PX 70  // worst fit error we accept before asking for another go
#define VERIFY_MS 20000

static struct {
    lv_obj_t *scr, *title, *body, *target, *ring, *done_row, *count;
    lv_timer_t *poll;
    int step;
    bool was_down, verifying;
    int samples;
    float sa, sb;
    float raw[CALIB_POINTS][2];
    uint32_t verify_since;
    void (*done)(void);
} c;

static void place_target(void)
{
    lv_point_t t = g_calib_targets[c.step];
    lv_obj_set_pos(c.target, t.x - 40, t.y - 40);
    lv_obj_remove_flag(c.target, LV_OBJ_FLAG_HIDDEN);
    dk_anim_pop(c.target, 0);
    lv_label_set_text_fmt(c.count, "%d of %d", c.step + 1, CALIB_POINTS);
}

static void set_text(const char *title, const char *body)
{
    lv_label_set_text(c.title, title);
    lv_label_set_text(c.body, body);
}

/** Least squares for v = p0*a + p1*b + p2 over the five taps. */
static bool fit(const float v[CALIB_POINTS], float p[3])
{
    double saa = 0, sab = 0, sa = 0, sbb = 0, sb = 0, n = CALIB_POINTS, sva = 0, svb = 0, sv = 0;
    for (int i = 0; i < CALIB_POINTS; i++) {
        double a = c.raw[i][0], b = c.raw[i][1];
        saa += a * a, sab += a * b, sa += a, sbb += b * b, sb += b;
        sva += v[i] * a, svb += v[i] * b, sv += v[i];
    }
    // Solve [saa sab sa; sab sbb sb; sa sb n] p = [sva svb sv] by Cramer's rule.
    double m[3][3] = {{saa, sab, sa}, {sab, sbb, sb}, {sa, sb, n}}, r[3] = {sva, svb, sv};
    double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                 m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (fabs(det) < 1e-6) return false;
    for (int k = 0; k < 3; k++) {
        double t[3][3];
        memcpy(t, m, sizeof(t));
        for (int i = 0; i < 3; i++) t[i][k] = r[i];
        double dk = t[0][0] * (t[1][1] * t[2][2] - t[1][2] * t[2][1]) - t[0][1] * (t[1][0] * t[2][2] - t[1][2] * t[2][0]) +
                    t[0][2] * (t[1][0] * t[2][1] - t[1][1] * t[2][0]);
        p[k] = (float)(dk / det);
    }
    return true;
}

static void restart(void);

static void finish_taps(void)
{
    float xs[CALIB_POINTS], ys[CALIB_POINTS], m[6];
    for (int i = 0; i < CALIB_POINTS; i++) xs[i] = (float)g_calib_targets[i].x, ys[i] = (float)g_calib_targets[i].y;
    bool ok = fit(xs, m) && fit(ys, m + 3);
    float worst = 0;
    for (int i = 0; ok && i < CALIB_POINTS; i++) {
        float x = m[0] * c.raw[i][0] + m[1] * c.raw[i][1] + m[2];
        float y = m[3] * c.raw[i][0] + m[4] * c.raw[i][1] + m[5];
        float e = sqrtf((x - xs[i]) * (x - xs[i]) + (y - ys[i]) * (y - ys[i]));
        if (e > worst) worst = e;
    }
    if (!ok || worst > MAX_ERR_PX) {
        ui_sound(DK_SND_OOPS);
        set_text("Hmm, let's try that again", "Tap right in the middle of each dot, one at a time.");
        c.step = 0;
        place_target();
        return;
    }
    if (g_platform && g_platform->touch_save_calibration) g_platform->touch_save_calibration(m);
    ui_sound(DK_SND_ALLOW);
    lv_obj_add_flag(c.target, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(c.count, "");
    set_text("That feels better!", "Tap Done to finish. If Done doesn't respond, wait a moment and we'll go again.");
    lv_obj_remove_flag(c.done_row, LV_OBJ_FLAG_HIDDEN);
    dk_anim_rise(c.done_row, 0);
    c.verifying = true;
    c.verify_since = lv_tick_get();
}

static void poll(lv_timer_t *t)
{
    (void)t;
    if (lv_screen_active() != c.scr || !g_platform || !g_platform->touch_raw) return;
    if (c.verifying) {
        if (lv_tick_elaps(c.verify_since) > VERIFY_MS) restart();
        return;
    }
    int32_t a, b;
    bool down = g_platform->touch_raw(&a, &b);
    if (down) {
        // Skip the first couple of samples while the finger settles, then average.
        if (++c.samples > 2) {
            c.sa += (float)a;
            c.sb += (float)b;
        }
    } else if (c.was_down) {
        int used = c.samples - 2;
        if (used >= 2) {
            c.raw[c.step][0] = c.sa / used;
            c.raw[c.step][1] = c.sb / used;
            ui_sound(DK_SND_TAP);
            if (++c.step == CALIB_POINTS) finish_taps();
            else place_target();
        }
        c.samples = 0;
        c.sa = c.sb = 0;
    }
    c.was_down = down;
}

static void finished(lv_event_t *e)
{
    (void)e;
    c.verifying = false;
    lv_obj_add_flag(c.done_row, LV_OBJ_FLAG_HIDDEN);
    if (c.done) c.done();
}

static void again(lv_event_t *e)
{
    (void)e;
    restart();
}

lv_obj_t *scr_calib_create(void)
{
    c.scr = lv_obj_create(NULL);
    lv_obj_remove_flag(c.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(c.scr, DK_GROUND, 0);

    lv_obj_t *mid = dk_box(c.scr);
    lv_obj_set_size(mid, 720, LV_SIZE_CONTENT);
    lv_obj_align(mid, LV_ALIGN_CENTER, 0, -140);
    dk_col(mid, 10);
    lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    c.title = dk_label(mid, DK_FONT_H2, DK_INK, "");
    c.body = dk_label(mid, DK_FONT_L, DK_MUTED, "");
    lv_obj_set_width(c.body, lv_pct(100));
    lv_label_set_long_mode(c.body, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(c.body, LV_TEXT_ALIGN_CENTER, 0);
    c.count = dk_label(mid, DK_FONT_M, DK_PURPLE, "");

    c.target = dk_box(c.scr);
    lv_obj_set_size(c.target, 80, 80);
    lv_obj_remove_flag(c.target, LV_OBJ_FLAG_CLICKABLE);
    dk_overflow(c.target, 60);
    c.ring = dk_card(c.target, DK_LILAC, LV_RADIUS_CIRCLE);
    lv_obj_set_size(c.ring, 80, 80);
    lv_obj_set_style_pad_all(c.ring, 0, 0);
    dk_anim_ping(c.ring, 1400, 0);
    lv_obj_t *dot = dk_card(c.target, DK_INK, LV_RADIUS_CIRCLE);
    lv_obj_set_size(dot, 22, 22);
    lv_obj_center(dot);

    c.done_row = dk_box(c.scr);
    lv_obj_set_size(c.done_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    dk_row(c.done_row, 18);
    lv_obj_align(c.done_row, LV_ALIGN_CENTER, 0, 120);
    lv_obj_t *agb = dk_button(c.done_row, "Do it again", false, DK_FONT_L);
    lv_obj_add_event_cb(agb, again, LV_EVENT_CLICKED, NULL);
    lv_obj_t *db = dk_button(c.done_row, "Done", true, DK_FONT_XL);
    lv_obj_set_width(db, 220);
    lv_obj_add_event_cb(db, finished, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(c.done_row, LV_OBJ_FLAG_HIDDEN);

    c.poll = lv_timer_create(poll, 15, NULL);
    return c.scr;
}

static void restart(void)
{
    c.step = 0;
    c.samples = 0;
    c.sa = c.sb = 0;
    c.was_down = false;
    c.verifying = false;
    lv_obj_add_flag(c.done_row, LV_OBJ_FLAG_HIDDEN);
    set_text("Let's line up your touch", "Tap the centre of each dot as it appears.");
    place_target();
}

void scr_calib_start(void (*done)(void))
{
    c.done = done;
    restart();
}
