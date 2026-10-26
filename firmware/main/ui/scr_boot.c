// Boot: Deskling pops onto the screen (the same lilac face-with-ears as the printed case),
// opens its eyes with the hello chirp, the wordmark drops in, a blink and a sparkle, then
// the UI fades in. About 2.6 s. Anything the UI is asked to show meanwhile waits for it.
#include <string.h>

#include "ui_internal.h"

#define BOOT_MS 2600
#define BODY_W 400
#define BODY_H 280
#define EAR_D 112

static struct {
    lv_obj_t *scr, *body, *ears[2], *eyes[2], *cheeks[2], *smile, *spark[3];
    lv_obj_t *letters[8];
    void (*done)(void);
} b;

static void set_scale(void *o, int32_t v)
{
    dk_style_scale(o, v);
}

static void set_scale_y(void *o, int32_t v)
{
    dk_style(o, LV_STYLE_TRANSFORM_SCALE_Y, v);
}

static void set_ty(void *o, int32_t v)
{
    dk_style(o, LV_STYLE_TRANSLATE_Y, v);
}

static void set_opa(void *o, int32_t v)
{
    dk_style(o, LV_STYLE_OPA, (lv_opa_t)v);
}

static void go(lv_obj_t *o, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms, uint32_t delay, lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, path ? path : lv_anim_path_ease_out);
    lv_anim_set_early_apply(&a, true);
    lv_anim_start(&a);
}

static void pivot_center(lv_obj_t *o, int32_t w, int32_t h)
{
    lv_obj_set_style_transform_pivot_x(o, w / 2, 0);
    lv_obj_set_style_transform_pivot_y(o, h / 2, 0);
}

static lv_obj_t *blob(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t c)
{
    lv_obj_t *o = dk_card(parent, c, LV_RADIUS_CIRCLE);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_pad_all(o, 0, 0);
    pivot_center(o, w, h);
    return o;
}

lv_obj_t *scr_boot_create(void)
{
    b.scr = lv_obj_create(NULL);
    lv_obj_remove_flag(b.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(b.scr, DK_GROUND, 0);

    // The whole character lives in one box so it can scale as one.
    const int32_t gw = BODY_W + 40, gh = BODY_H + EAR_D / 2 + 20;
    lv_obj_t *g = dk_box(b.scr);
    lv_obj_set_size(g, gw, gh);
    lv_obj_align(g, LV_ALIGN_CENTER, 0, -70);
    pivot_center(g, gw, gh);
    dk_overflow(g, 60);
    b.body = g;

    const int32_t bx = 20, by = EAR_D / 2;
    for (int i = 0; i < 2; i++) {
        int32_t ex = i ? bx + BODY_W - 96 - EAR_D / 2 : bx + 96 - EAR_D / 2;
        b.ears[i] = blob(g, ex, 6, EAR_D, EAR_D, DK_LILAC);
        lv_obj_t *inner = blob(b.ears[i], EAR_D / 4, EAR_D / 4, EAR_D / 2, EAR_D / 2, lv_color_hex(0xBDB1EA));
        (void)inner;
    }
    lv_obj_t *bezel = dk_card(g, DK_LILAC, 64);
    lv_obj_set_size(bezel, BODY_W, BODY_H);
    lv_obj_set_pos(bezel, bx, by);
    lv_obj_set_style_pad_all(bezel, 0, 0);

    // Peach "cheek" buttons on the bezel's bottom corners, like the case.
    blob(bezel, 18, BODY_H - 44, 26, 26, DK_PEACH);
    blob(bezel, BODY_W - 44, BODY_H - 44, 26, 26, DK_PEACH);

    lv_obj_t *screen = dk_card(bezel, DK_INK, 26);
    lv_obj_set_size(screen, BODY_W - 64, BODY_H - 84);
    lv_obj_set_pos(screen, 32, 30);
    lv_obj_set_style_pad_all(screen, 0, 0);

    const int32_t sw = BODY_W - 64, sh = BODY_H - 84;
    for (int i = 0; i < 2; i++) {
        int32_t cx = sw / 2 + (i ? 50 : -50);
        b.eyes[i] = blob(screen, cx - 15, sh / 2 - 34, 30, 42, DK_GROUND);
        b.cheeks[i] = blob(screen, cx + (i ? 30 : -30) - 20, sh / 2 + 14, 40, 22, DK_PEACH);
    }
    b.smile = blob(screen, sw / 2 - 16, sh / 2 + 10, 32, 16, DK_GROUND);
    lv_obj_set_style_radius(b.smile, 4, 0);
    lv_obj_set_style_radius(b.smile, 16, 0);

    for (int i = 0; i < 3; i++) {
        static const int32_t sx[] = {-6, gw - 40, gw - 70}, sy[] = {70, 40, gh - 70}, sz[] = {30, 24, 18};
        b.spark[i] = blob(g, sx[i], sy[i], sz[i], sz[i], DK_SPARK);
        lv_obj_set_style_radius(b.spark[i], sz[i] / 3, 0);
        lv_obj_set_style_transform_rotation(b.spark[i], 450, 0);
    }

    // Wordmark, one label per letter so they can drop in one by one.
    lv_obj_t *word = dk_box(b.scr);
    lv_obj_set_size(word, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    dk_row(word, 0);
    lv_obj_align(word, LV_ALIGN_CENTER, 0, 170);
    dk_overflow(word, 40);
    const char *name = "deskling";
    for (int i = 0; i < 8; i++) {
        char ch[2] = {name[i], 0};
        b.letters[i] = dk_label(word, DK_FONT_H1, DK_INK, ch);
    }
    return b.scr;
}

static void hello(lv_timer_t *t)
{
    (void)t;
    if (g_platform && g_platform->play_sound) g_platform->play_sound(DK_SND_HELLO);
}

static void blink(lv_timer_t *t)
{
    (void)t;
    for (int i = 0; i < 2; i++) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, b.eyes[i]);
        lv_anim_set_exec_cb(&a, set_scale_y);
        lv_anim_set_values(&a, 256, 26);
        lv_anim_set_duration(&a, 90);
        lv_anim_set_reverse_duration(&a, 120);
        lv_anim_start(&a);
    }
}

static void finish(lv_timer_t *t)
{
    (void)t;
    if (b.done) b.done();
}

void scr_boot_play(void (*done)(void))
{
    b.done = done;
    // Pop in with a squash-and-stretch overshoot.
    go(b.body, set_scale, 60, 256, 620, 80, lv_anim_path_overshoot);
    go(b.body, set_opa, 0, 255, 200, 80, NULL);
    // Ears spring up a moment later.
    for (int i = 0; i < 2; i++) go(b.ears[i], set_ty, 50, 0, 420, 360 + i * 90, lv_anim_path_overshoot);
    // Eyes open (with the chirp), cheeks blush in.
    for (int i = 0; i < 2; i++) {
        go(b.eyes[i], set_scale_y, 10, 256, 260, 820, lv_anim_path_overshoot);
        go(b.cheeks[i], set_opa, 0, 255, 300, 900, NULL);
    }
    go(b.smile, set_scale, 0, 256, 300, 960, lv_anim_path_overshoot);
    {
        // A little hop as the eyes open: up and straight back down.
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, b.body);
        lv_anim_set_exec_cb(&a, set_ty);
        lv_anim_set_values(&a, 0, -18);
        lv_anim_set_duration(&a, 170);
        lv_anim_set_delay(&a, 960);
        lv_anim_set_reverse_duration(&a, 260);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
    for (int i = 0; i < 3; i++) {
        go(b.spark[i], set_scale, 0, 256, 320, 1250 + i * 140, lv_anim_path_overshoot);
        go(b.spark[i], set_opa, 255, 0, 400, 2050 + i * 80, NULL);
    }
    for (int i = 0; i < 8; i++) {
        go(b.letters[i], set_ty, -40, 0, 380, 1050 + i * 55, lv_anim_path_overshoot);
        go(b.letters[i], set_opa, 0, 255, 200, 1050 + i * 55, NULL);
    }
    lv_timer_t *bl = lv_timer_create(blink, 1950, NULL);
    lv_timer_set_repeat_count(bl, 1);
    lv_timer_t *t = lv_timer_create(hello, 820, NULL);
    lv_timer_set_repeat_count(t, 1);
    lv_timer_t *f = lv_timer_create(finish, BOOT_MS, NULL);
    lv_timer_set_repeat_count(f, 1);
}
