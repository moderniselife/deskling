// Reusable motion: everything is translate / scale / opacity so it maps to cheap LVGL draws.
#pragma once

#include "lvgl.h"

/** Fade + rise into place after `delay_ms` (card entrances). */
void dk_anim_rise(lv_obj_t *o, uint32_t delay_ms);
/** Pop in from small with overshoot. */
void dk_anim_pop(lv_obj_t *o, uint32_t delay_ms);
/** Slide down from above while fading in (feed items, banners). */
void dk_anim_drop_in(lv_obj_t *o, int32_t from_y, uint32_t delay_ms);
/** Endless gentle up-down bob. */
void dk_anim_bob(lv_obj_t *o, int32_t amp, uint32_t period_ms, uint32_t delay_ms);
/** Endless ping ring: scale up + fade out. */
void dk_anim_ping(lv_obj_t *o, uint32_t period_ms, uint32_t delay_ms);
/** Endless opacity breathing between lo and hi. */
void dk_anim_breathe_opa(lv_obj_t *o, lv_opa_t lo, lv_opa_t hi, uint32_t period_ms, uint32_t delay_ms);
/** Endless vertical "equalizer" bar (height from 30% to 100% of h). */
void dk_anim_eq(lv_obj_t *o, int32_t h, uint32_t period_ms, uint32_t delay_ms);
/** A little wiggle every few seconds. */
void dk_anim_wiggle(lv_obj_t *o);
/** Glowing pulse for primary buttons (shadow spread). */
void dk_anim_glow(lv_obj_t *o, lv_color_t color);
/** Blink once a second (clock colon). */
void dk_anim_blink(lv_obj_t *o);
/** Shake side to side a few times (attention). */
void dk_anim_shake(lv_obj_t *o);
/** Press feedback: shrink a little while pressed. Apply to any clickable. */
void dk_press_style(lv_obj_t *o);
