#include "deskling.h"

#include <stdlib.h>

#include "theme.h"

// All geometry is in the 200-unit space of the design's SVG, scaled to `s`.
#define K(v) ((int32_t)((v) * f->s / 200))

typedef struct {
    int32_t s;
    dk_mood_t mood, base;
    lv_timer_t *timer;
    lv_obj_t *root, *body_g, *body, *eyes_g;
    lv_obj_t *eye[2], *eye_slot[2], *shine[2], *closed[2], *cheek[2];
    lv_obj_t *smile, *ooh, *snore, *grin, *flat, *brow[2];
    lv_obj_t *think, *think_dot[3], *sweat, *qbubble, *z[3], *spark[3];
    int32_t eye_h;
} face_t;

static face_t *get(lv_obj_t *d)
{
    return (face_t *)lv_obj_get_user_data(d);
}

// ------------------------------------------------------------ building

static lv_obj_t *blob(lv_obj_t *parent, int32_t cx, int32_t cy, int32_t w, int32_t h, lv_color_t c)
{
    lv_obj_t *o = dk_box(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, cx - w / 2, cy - h / 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *arc(lv_obj_t *parent, int32_t cx, int32_t cy, int32_t d, int32_t w, int32_t a0, int32_t a1)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_remove_style_all(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(a, d, d);
    lv_obj_set_pos(a, cx - d / 2, cy - d / 2);
    lv_arc_set_bg_angles(a, a0, a1);
    lv_arc_set_angles(a, a0, a1);
    lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, DK_INK, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(a, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    return a;
}

static void on_delete(lv_event_t *e)
{
    face_t *f = lv_event_get_user_data(e);
    if (f->timer) lv_timer_delete(f->timer);
    free(f);
}

lv_obj_t *deskling_create(lv_obj_t *parent, int32_t size, lv_color_t body)
{
    face_t *f = calloc(1, sizeof(face_t));
    f->s = size;
    f->mood = f->base = DK_MOOD_CALM;

    f->root = dk_box(parent);
    lv_obj_set_size(f->root, size, size);
    dk_overflow(f->root, size / 2);
    lv_obj_set_user_data(f->root, f);
    lv_obj_add_event_cb(f->root, on_delete, LV_EVENT_DELETE, f);

    f->body_g = dk_box(f->root);
    lv_obj_set_size(f->body_g, size, size);
    dk_overflow(f->body_g, size / 4);
    lv_obj_set_style_transform_pivot_x(f->body_g, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(f->body_g, lv_pct(88), 0);

    f->body = dk_box(f->body_g);
    lv_obj_set_pos(f->body, K(20), K(28));
    lv_obj_set_size(f->body, K(160), K(150));
    lv_obj_set_style_radius(f->body, K(72), 0);
    lv_obj_set_style_bg_color(f->body, body, 0);
    lv_obj_set_style_bg_opa(f->body, LV_OPA_COVER, 0);

    f->cheek[0] = blob(f->body_g, K(54), K(124), K(26), K(16), DK_PEACH);
    f->cheek[1] = blob(f->body_g, K(146), K(124), K(26), K(16), DK_PEACH);

    // Eyes sit in fixed slots so they stay centred when they squash for a blink.
    f->eyes_g = dk_box(f->body_g);
    lv_obj_set_size(f->eyes_g, size, size);
    dk_overflow(f->eyes_g, size / 8);
    f->eye_h = K(30);
    const int32_t ex[2] = {74, 126};
    for (int i = 0; i < 2; i++) {
        f->eye_slot[i] = dk_box(f->eyes_g);
        lv_obj_set_size(f->eye_slot[i], K(22), K(30));
        lv_obj_set_pos(f->eye_slot[i], K(ex[i] - 11), K(98 - 15));
        dk_overflow(f->eye_slot[i], K(8));
        f->eye[i] = dk_box(f->eye_slot[i]);
        lv_obj_set_size(f->eye[i], K(22), K(30));
        lv_obj_align(f->eye[i], LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_radius(f->eye[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(f->eye[i], DK_INK, 0);
        lv_obj_set_style_bg_opa(f->eye[i], LV_OPA_COVER, 0);
        lv_obj_set_user_data(f->eye[i], f);
        f->shine[i] = dk_box(f->eye[i]);
        lv_obj_set_size(f->shine[i], K(8), K(8));
        lv_obj_align(f->shine[i], LV_ALIGN_TOP_RIGHT, -K(3), K(4));
        lv_obj_set_style_radius(f->shine[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(f->shine[i], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(f->shine[i], LV_OPA_COVER, 0);
        f->closed[i] = arc(f->body_g, K(ex[i]), K(100), K(28), K(6), 20, 160);
    }

    f->smile = arc(f->body_g, K(100), K(120), K(28), K(6), 30, 150);
    f->ooh = blob(f->body_g, K(100), K(140), K(18), K(22), DK_INK);
    f->snore = blob(f->body_g, K(100), K(142), K(12), K(10), DK_INK);
    f->grin = arc(f->body_g, K(100), K(122), K(38), K(19), 0, 180);
    f->flat = arc(f->body_g, K(100), K(142), K(22), K(5), 220, 320);

    for (int i = 0; i < 2; i++) {
        f->brow[i] = dk_box(f->body_g);
        lv_obj_set_size(f->brow[i], K(28), K(6));
        lv_obj_set_pos(f->brow[i], K(i ? 114 : 58), K(70));
        lv_obj_set_style_radius(f->brow[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(f->brow[i], DK_INK, 0);
        lv_obj_set_style_bg_opa(f->brow[i], LV_OPA_COVER, 0);
        lv_obj_set_style_transform_pivot_x(f->brow[i], lv_pct(50), 0);
        lv_obj_set_style_transform_pivot_y(f->brow[i], lv_pct(50), 0);
        lv_obj_set_style_transform_rotation(f->brow[i], i ? 150 : -150, 0); // inner ends up: worried, not cross
    }

    // Thinking bubble with three dots.
    f->think = dk_card(f->root, DK_CARD, LV_RADIUS_CIRCLE);
    lv_obj_set_style_pad_hor(f->think, K(12), 0);
    lv_obj_set_style_pad_ver(f->think, K(9), 0);
    dk_row(f->think, K(6));
    lv_obj_set_pos(f->think, K(132), K(-2));
    for (int i = 0; i < 3; i++) {
        f->think_dot[i] = dk_box(f->think);
        lv_obj_set_size(f->think_dot[i], K(10), K(10));
        lv_obj_set_style_radius(f->think_dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(f->think_dot[i], DK_INK, 0);
        lv_obj_set_style_bg_opa(f->think_dot[i], LV_OPA_COVER, 0);
    }

    f->sweat = dk_box(f->root);
    lv_obj_set_size(f->sweat, K(14), K(20));
    lv_obj_set_pos(f->sweat, K(158), K(40));
    lv_obj_set_style_radius(f->sweat, K(7), 0);
    lv_obj_set_style_bg_color(f->sweat, DK_SWEAT, 0);
    lv_obj_set_style_bg_opa(f->sweat, LV_OPA_COVER, 0);

    f->qbubble = dk_box(f->root);
    lv_obj_set_size(f->qbubble, K(62), K(62));
    lv_obj_set_pos(f->qbubble, K(150), K(-8));
    lv_obj_set_style_radius(f->qbubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(f->qbubble, DK_INK, 0);
    lv_obj_set_style_bg_opa(f->qbubble, LV_OPA_COVER, 0);
    lv_obj_t *q = dk_label(f->qbubble, size >= 220 ? DK_FONT_H2 : DK_FONT_XL, DK_BUTTER, "?");
    lv_obj_center(q);

    for (int i = 0; i < 3; i++) {
        f->z[i] = dk_label(f->root, i == 0 ? DK_FONT_XL : DK_FONT_L, DK_NIGHT_MUTED, "z");
        lv_obj_set_pos(f->z[i], K(162), K(30));
    }

    const int32_t sp[3][2] = {{24, 24}, {170, 14}, {178, 150}};
    for (int i = 0; i < 3; i++) {
        f->spark[i] = dk_box(f->root);
        lv_obj_set_size(f->spark[i], K(16), K(16));
        lv_obj_set_pos(f->spark[i], K(sp[i][0]), K(sp[i][1]));
        lv_obj_set_style_radius(f->spark[i], K(3), 0);
        lv_obj_set_style_bg_color(f->spark[i], DK_SPARK, 0);
        lv_obj_set_style_bg_opa(f->spark[i], LV_OPA_COVER, 0);
        lv_obj_set_style_transform_pivot_x(f->spark[i], lv_pct(50), 0);
        lv_obj_set_style_transform_pivot_y(f->spark[i], lv_pct(50), 0);
    }

    deskling_set_mood(f->root, DK_MOOD_CALM);
    return f->root;
}

// ------------------------------------------------------------- motion

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void reset(lv_obj_t *o)
{
    lv_anim_delete(o, NULL);
    lv_obj_set_style_translate_x(o, 0, 0);
    lv_obj_set_style_translate_y(o, 0, 0);
    lv_obj_set_style_opa(o, LV_OPA_COVER, 0);
}

static void anim(lv_obj_t *o, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t dur, uint32_t back, uint32_t delay,
                 uint32_t repeat_delay, lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, dur);
    if (back) lv_anim_set_reverse_duration(&a, back);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_repeat_delay(&a, repeat_delay);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, path ? path : lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void ex_ty(void *o, int32_t v) { lv_obj_set_style_translate_y(o, v, 0); }
static void ex_tx(void *o, int32_t v) { lv_obj_set_style_translate_x(o, v, 0); }
static void ex_opa(void *o, int32_t v) { lv_obj_set_style_opa(o, (lv_opa_t)v, 0); }
static void ex_rot(void *o, int32_t v) { lv_obj_set_style_transform_rotation(o, v, 0); }
static void ex_scale(void *o, int32_t v) { lv_obj_set_style_transform_scale(o, v, 0); }

static void ex_blink(void *o, int32_t v)
{
    face_t *f = lv_obj_get_user_data(o);
    int32_t h = f->eye_h;
    if (v > 910 && v < 970) {
        int32_t t = v < 940 ? v - 910 : 970 - v; // 0..30
        h = f->eye_h - (f->eye_h * 9 * t) / (10 * 30);
    }
    lv_obj_set_height(o, h);
}

static int32_t lerp_keys(int32_t v, const int32_t (*k)[2], int n)
{
    for (int i = 1; i < n; i++) {
        if (v <= k[i][0]) {
            int32_t span = k[i][0] - k[i - 1][0];
            if (span <= 0) return k[i][1];
            return k[i - 1][1] + ((k[i][1] - k[i - 1][1]) * (v - k[i - 1][0])) / span;
        }
    }
    return k[n - 1][1];
}

static void ex_glance(void *o, int32_t v)
{
    // Look right, then left, then back: v is 0..1000 over 7 s; values are in 1/100 px of k.
    face_t *f = lv_obj_get_user_data(lv_obj_get_parent(lv_obj_get_parent(o)));
    static const int32_t keys[][2] = {{0, 0}, {280, 0}, {340, 700}, {520, 700}, {580, -600}, {780, -600}, {840, 0}, {1000, 0}};
    lv_obj_set_style_translate_x(o, (lerp_keys(v, keys, 8) * f->s) / 20000, 0);
}

static void ex_dart(void *o, int32_t v)
{
    face_t *f = lv_obj_get_user_data(lv_obj_get_parent(lv_obj_get_parent(o)));
    static const int32_t keys[][2] = {{0, 0}, {200, 0}, {300, -700}, {450, -700}, {550, 700}, {700, 700}, {800, 0}, {1000, 0}};
    lv_obj_set_style_translate_x(o, (lerp_keys(v, keys, 8) * f->s) / 20000, 0);
}

static void ex_sweat(void *o, int32_t v)
{
    face_t *f = lv_obj_get_user_data(lv_obj_get_parent(o));
    lv_obj_set_style_translate_y(o, K(-6) + (K(40) * v) / 1000, 0);
    lv_obj_set_style_opa(o, (lv_opa_t)(v < 200 ? (v * 255) / 200 : (255 * (1000 - v)) / 800), 0);
}

static void ex_z(void *o, int32_t v)
{
    face_t *f = lv_obj_get_user_data(lv_obj_get_parent(o));
    lv_obj_set_style_translate_x(o, (K(30) * v) / 1000, 0);
    lv_obj_set_style_translate_y(o, -(K(64) * v) / 1000, 0);
    lv_obj_set_style_opa(o, (lv_opa_t)(v < 250 ? (v * 255) / 250 : (255 * (1000 - v)) / 750), 0);
}

static void ex_spark(void *o, int32_t v)
{
    lv_obj_set_style_transform_scale(o, 50 + (290 * v) / 1000, 0);
    lv_obj_set_style_transform_rotation(o, 450 + (600 * v) / 1000, 0);
    lv_obj_set_style_opa(o, (lv_opa_t)(v < 400 ? (v * 255) / 400 : (255 * (1000 - v)) / 600), 0);
}

static void ex_eye_tick(void *o, int32_t v)
{
    ex_blink(o, v);
}

static void start_blink(face_t *f, uint32_t period)
{
    for (int i = 0; i < 2; i++) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, f->eye[i]);
        lv_anim_set_exec_cb(&a, ex_eye_tick);
        lv_anim_set_values(&a, 0, 1000);
        lv_anim_set_duration(&a, period);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }
}

static void loop01(lv_obj_t *o, lv_anim_exec_xcb_t cb, uint32_t period, uint32_t delay)
{
    anim(o, cb, 0, 1000, period, 0, delay, 0, lv_anim_path_linear);
}

void deskling_set_mood(lv_obj_t *d, dk_mood_t mood)
{
    face_t *f = get(d);
    if (!f) return;
    f->mood = mood;
    if (mood != DK_MOOD_HAPPY) f->base = mood;

    lv_obj_t *all[] = {f->body_g, f->eyes_g, f->eye[0], f->eye[1], f->cheek[0], f->cheek[1], f->think, f->think_dot[0],
                       f->think_dot[1], f->think_dot[2], f->sweat, f->qbubble, f->z[0], f->z[1], f->z[2], f->spark[0],
                       f->spark[1], f->spark[2], f->brow[0], f->brow[1]};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) reset(all[i]);
    lv_obj_set_style_transform_rotation(f->body_g, 0, 0);
    lv_obj_set_style_transform_scale(f->body_g, 256, 0);
    lv_obj_set_height(f->eye[0], f->eye_h);
    lv_obj_set_height(f->eye[1], f->eye_h);

    bool eyes_open = mood != DK_MOOD_HAPPY && mood != DK_MOOD_SLEEPY;
    show(f->eyes_g, eyes_open);
    show(f->closed[0], !eyes_open);
    show(f->closed[1], !eyes_open);
    if (mood == DK_MOOD_HAPPY) {
        lv_arc_set_angles(f->closed[0], 200, 340);
        lv_arc_set_angles(f->closed[1], 200, 340);
    } else {
        lv_arc_set_angles(f->closed[0], 20, 160);
        lv_arc_set_angles(f->closed[1], 20, 160);
    }
    show(f->smile, mood == DK_MOOD_CALM || mood == DK_MOOD_BUSY);
    show(f->ooh, mood == DK_MOOD_WORRIED);
    show(f->grin, mood == DK_MOOD_HAPPY);
    show(f->flat, mood == DK_MOOD_CURIOUS);
    show(f->snore, mood == DK_MOOD_SLEEPY);
    show(f->brow[0], mood == DK_MOOD_WORRIED);
    show(f->brow[1], mood == DK_MOOD_WORRIED);
    show(f->think, mood == DK_MOOD_BUSY);
    show(f->sweat, mood == DK_MOOD_WORRIED);
    show(f->qbubble, mood == DK_MOOD_CURIOUS);
    for (int i = 0; i < 3; i++) {
        show(f->z[i], mood == DK_MOOD_SLEEPY);
        show(f->spark[i], mood == DK_MOOD_HAPPY);
    }

    switch (mood) {
    case DK_MOOD_CALM:
        start_blink(f, 5200);
        anim(f->body_g, ex_ty, 0, -K(4), 2000, 2000, 0, 0, NULL);
        break;
    case DK_MOOD_BUSY:
        start_blink(f, 4600);
        anim(f->body_g, ex_ty, 0, -K(8), 1600, 1600, 0, 0, NULL);
        loop01(f->eyes_g, ex_glance, 7000, 0);
        anim(f->cheek[0], ex_opa, 180, 255, 1200, 1200, 0, 0, NULL);
        anim(f->cheek[1], ex_opa, 180, 255, 1200, 1200, 0, 0, NULL);
        for (int i = 0; i < 3; i++) anim(f->think_dot[i], ex_ty, 0, -K(8), 300, 300, i * 150, 600, NULL);
        break;
    case DK_MOOD_WORRIED:
        anim(f->body_g, ex_tx, -K(3), K(3), 125, 125, 0, 0, NULL);
        loop01(f->eyes_g, ex_dart, 2400, 0);
        anim(f->brow[0], ex_ty, 0, -K(4), 600, 600, 0, 0, NULL);
        anim(f->brow[1], ex_ty, 0, -K(4), 600, 600, 0, 0, NULL);
        loop01(f->sweat, ex_sweat, 1800, 0);
        break;
    case DK_MOOD_CURIOUS:
        start_blink(f, 4000);
        anim(f->body_g, ex_rot, -60, 60, 1300, 1300, 0, 0, NULL);
        anim(f->qbubble, ex_ty, 0, -K(8), 800, 800, 0, 0, NULL);
        break;
    case DK_MOOD_HAPPY:
        anim(f->body_g, ex_ty, 0, -K(20), 280, 320, 0, 80, lv_anim_path_ease_out);
        for (int i = 0; i < 3; i++) loop01(f->spark[i], ex_spark, 1000, i * 330);
        break;
    case DK_MOOD_SLEEPY:
        anim(f->body_g, ex_scale, 256, 266, 2250, 2250, 0, 0, NULL);
        for (int i = 0; i < 3; i++) loop01(f->z[i], ex_z, 3000, i * 1000);
        break;
    }
}

dk_mood_t deskling_get_mood(lv_obj_t *d)
{
    face_t *f = get(d);
    return f ? f->mood : DK_MOOD_CALM;
}

static void celebrate_done(lv_timer_t *t)
{
    face_t *f = lv_timer_get_user_data(t);
    f->timer = NULL;
    deskling_set_mood(f->root, f->base);
}

void deskling_celebrate(lv_obj_t *d, uint32_t ms)
{
    face_t *f = get(d);
    if (!f) return;
    dk_mood_t base = f->mood == DK_MOOD_HAPPY ? f->base : f->mood;
    deskling_set_mood(d, DK_MOOD_HAPPY);
    f->base = base;
    if (f->timer) lv_timer_delete(f->timer);
    f->timer = lv_timer_create(celebrate_done, ms, f);
    lv_timer_set_repeat_count(f->timer, 1);
}
