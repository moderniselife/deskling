#include "anim.h"

static void set_opa(void *o, int32_t v)
{
    lv_obj_set_style_opa(o, (lv_opa_t)v, 0);
}
static void set_ty(void *o, int32_t v)
{
    lv_obj_set_style_translate_y(o, v, 0);
}
static void set_tx(void *o, int32_t v)
{
    lv_obj_set_style_translate_x(o, v, 0);
}
static void set_scale(void *o, int32_t v)
{
    lv_obj_set_style_transform_scale(o, v, 0);
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

static void ping_exec(void *o, int32_t v)
{
    // v: 0..1000 → scale 1.0 → 2.8, opacity 55% → 0
    lv_obj_set_style_transform_scale(o, 256 + (v * 461) / 1000, 0);
    lv_obj_set_style_opa(o, (lv_opa_t)(140 - (v * 140) / 1000), 0);
}

void dk_anim_ping(lv_obj_t *o, uint32_t period_ms, uint32_t delay_ms)
{
    center_pivot(o);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, ping_exec);
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
    lv_obj_set_style_transform_rotation(o, rot, 0);
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
    lv_obj_set_style_shadow_spread(o, (v * 14) / 1000, 0);
    lv_obj_set_style_shadow_opa(o, (lv_opa_t)(110 - (v * 110) / 1000), 0);
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
    lv_obj_set_style_opa(o, v < 500 ? LV_OPA_COVER : LV_OPA_20, 0);
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
    lv_obj_set_style_translate_x(o, (x * (1000 - v)) / 1000, 0);
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
    (void)set_tx;
}
