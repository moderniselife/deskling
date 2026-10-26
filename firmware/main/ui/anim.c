#include "anim.h"
#include "ui.h"

void dk_style(lv_obj_t *o, lv_style_prop_t prop, int32_t v)
{
    lv_style_value_t cur;
    if (lv_obj_get_local_style_prop(o, prop, &cur, 0) == LV_STYLE_RES_FOUND && cur.num == v) return;
    lv_style_value_t nv = {.num = v};
    lv_obj_set_local_style_prop(o, prop, nv, 0);
}

void dk_style_scale(lv_obj_t *o, int32_t v)
{
    dk_style(o, LV_STYLE_TRANSFORM_SCALE_X, v);
    dk_style(o, LV_STYLE_TRANSFORM_SCALE_Y, v);
}

static void set_opa(void *o, int32_t v)
{
    dk_style(o, LV_STYLE_OPA, (lv_opa_t)v);
}
static void set_ty(void *o, int32_t v)
{
    dk_style(o, LV_STYLE_TRANSLATE_Y, v);
}
static void set_tx(void *o, int32_t v)
{
    dk_style(o, LV_STYLE_TRANSLATE_X, v);
}
static void set_scale(void *o, int32_t v)
{
    dk_style_scale(o, v);
}
static void set_height(void *o, int32_t v)
{
    lv_obj_set_height(o, v);
}

static void center_pivot(lv_obj_t *o)
{
    lv_obj_set_style_transform_pivot_x(o, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(o, lv_pct(50), 0);
}

static void start(lv_obj_t *o, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t dur, uint32_t delay, lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, dur);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

static void loop(lv_obj_t *o, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t half, uint32_t delay)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, half);
    lv_anim_set_reverse_duration(&a, half);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void dk_anim_rise(lv_obj_t *o, uint32_t delay_ms)
{
    lv_obj_set_style_opa(o, LV_OPA_TRANSP, 0);
    start(o, set_opa, LV_OPA_TRANSP, LV_OPA_COVER, 450, delay_ms, lv_anim_path_ease_out);
    start(o, set_ty, 18, 0, 600, delay_ms, lv_anim_path_overshoot);
}

void dk_anim_pop(lv_obj_t *o, uint32_t delay_ms)
{
    center_pivot(o);
    lv_obj_set_style_transform_scale(o, 60, 0);
    start(o, set_scale, 60, 256, 420, delay_ms, lv_anim_path_overshoot);
}

void dk_anim_drop_in(lv_obj_t *o, int32_t from_y, uint32_t delay_ms)
{
    lv_obj_set_style_opa(o, LV_OPA_TRANSP, 0);
    start(o, set_opa, LV_OPA_TRANSP, LV_OPA_COVER, 350, delay_ms, lv_anim_path_ease_out);
    start(o, set_ty, from_y, 0, 550, delay_ms, lv_anim_path_overshoot);
}

void dk_anim_bob(lv_obj_t *o, int32_t amp, uint32_t period_ms, uint32_t delay_ms)
{
    loop(o, set_ty, 0, -amp, period_ms / 2, delay_ms);
}

static void ping_exec(lv_anim_t *a, int32_t v)
{
    // v: 0..1000 → scale 1.0 → 2.8, opacity 55% → 0. Drawn by growing the ring itself
    // (transform_width/height on both sides, border scaled to match) rather than scaling it
    // as a picture, which would push the whole ring through an offscreen layer every frame.
    lv_obj_t *o = a->var;
    uint32_t base = (uint32_t)(uintptr_t)lv_anim_get_user_data(a);
    int32_t w = (int32_t)(base & 0xFFFF), border = (int32_t)(base >> 16);
    int32_t scale = 256 + (v * 461) / 1000;
    int32_t grow = (w * (scale - 256) + 256) / 512; // per side
    dk_style(o, LV_STYLE_TRANSFORM_WIDTH, grow);
    dk_style(o, LV_STYLE_TRANSFORM_HEIGHT, grow);
    if (border) dk_style(o, LV_STYLE_BORDER_WIDTH, (border * scale + 128) / 256);
    dk_style(o, LV_STYLE_OPA, (lv_opa_t)(140 - (v * 140) / 1000));
}

void dk_anim_ping(lv_obj_t *o, uint32_t period_ms, uint32_t delay_ms)
{
    int32_t w = lv_obj_get_style_width(o, 0);
    int32_t border = lv_obj_get_style_border_width(o, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_custom_exec_cb(&a, ping_exec);
    lv_anim_set_user_data(&a, (void *)(uintptr_t)((uint32_t)w | ((uint32_t)border << 16)));
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, period_ms);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void dk_anim_breathe_opa(lv_obj_t *o, lv_opa_t lo, lv_opa_t hi, uint32_t period_ms, uint32_t delay_ms)
{
    loop(o, set_opa, lo, hi, period_ms / 2, delay_ms);
}

void dk_anim_eq(lv_obj_t *o, int32_t h, uint32_t period_ms, uint32_t delay_ms)
{
    loop(o, set_height, (h * 30) / 100, h, period_ms / 2, delay_ms);
}

static void wiggle_exec(void *o, int32_t v)
{
    // Still most of the time, then a quick left-right-left at the end of each cycle.
    int32_t rot = 0;
    if (v > 860) {
        int32_t t = v - 860; // 0..140
        rot = (lv_trigo_sin((int16_t)(t * 360 * 2 / 140)) * 70) / LV_TRIGO_SIN_MAX;
        rot = (rot * (140 - t)) / 140;
    }
    dk_style(o, LV_STYLE_TRANSFORM_ROTATION, rot);
}

void dk_anim_wiggle(lv_obj_t *o)
{
    center_pivot(o);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, wiggle_exec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 3500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void glow_exec(void *o, int32_t v)
{
    dk_style(o, LV_STYLE_SHADOW_SPREAD, (v * 14) / 1000);
    dk_style(o, LV_STYLE_SHADOW_OPA, (lv_opa_t)(110 - (v * 110) / 1000));
}

void dk_anim_glow(lv_obj_t *o, lv_color_t color)
{
    lv_obj_set_style_shadow_color(o, color, 0);
    lv_obj_set_style_shadow_width(o, 4, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, glow_exec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 1600);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void blink_exec(void *o, int32_t v)
{
    dk_style(o, LV_STYLE_OPA, v < 500 ? LV_OPA_COVER : LV_OPA_20);
}

void dk_anim_blink(lv_obj_t *o)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, blink_exec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void shake_exec(void *o, int32_t v)
{
    int32_t x = (lv_trigo_sin((int16_t)((v * 360 * 4) / 1000)) * 10) / LV_TRIGO_SIN_MAX;
    dk_style(o, LV_STYLE_TRANSLATE_X, (x * (1000 - v)) / 1000);
}

void dk_anim_shake(lv_obj_t *o)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, shake_exec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 600);
    lv_anim_start(&a);
}

static void press_sound(lv_event_t *e)
{
    (void)e;
    ui_sound(DK_SND_TAP);
}

void dk_press_style(lv_obj_t *o)
{
    static lv_style_t pressed;
    static lv_style_transition_dsc_t tr;
    static bool ready;
    if (!ready) {
        static const lv_style_prop_t props[] = {LV_STYLE_TRANSFORM_SCALE_X, LV_STYLE_TRANSFORM_SCALE_Y, 0};
        lv_style_transition_dsc_init(&tr, props, lv_anim_path_ease_out, 120, 0, NULL);
        lv_style_init(&pressed);
        lv_style_set_transform_scale(&pressed, 240);
        lv_style_set_transition(&pressed, &tr);
        ready = true;
    }
    center_pivot(o);
    lv_obj_set_style_transition(o, &tr, 0);
    lv_obj_add_style(o, &pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(o, press_sound, LV_EVENT_PRESSED, NULL);
    (void)set_tx;
}
