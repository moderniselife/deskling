// Colours, fonts and small style helpers from docs/design.md.
#pragma once

#include "lvgl.h"

#define DK_W 1280
#define DK_H 800

// Day palette
#define DK_GROUND lv_color_hex(0xFBF6EF)
#define DK_CARD lv_color_hex(0xFFFFFF)
#define DK_INK lv_color_hex(0x2A2433)
#define DK_INK_SOFT lv_color_hex(0x3A3346)
#define DK_MUTED lv_color_hex(0x6A6274)
#define DK_LINE lv_color_hex(0xF3EDE4)
#define DK_FACE lv_color_hex(0xFFF9F2)
#define DK_PEACH lv_color_hex(0xF4B393)
#define DK_LILAC lv_color_hex(0xCFC4F2)
#define DK_LILAC_SOFT lv_color_hex(0xE7E2FA)
#define DK_PURPLE lv_color_hex(0x5B4FA8)
#define DK_ALERT_BG lv_color_hex(0xFDE8E1)
#define DK_ALERT_PILL lv_color_hex(0xFAD9CF)
#define DK_ALERT_INK lv_color_hex(0xC8553D)
#define DK_ALERT_RING lv_color_hex(0xE8806A)
#define DK_GOOD_BG lv_color_hex(0xD3EEDF)
#define DK_GOOD_INK lv_color_hex(0x2F8A63)
#define DK_WARM lv_color_hex(0xFDE9DC)
#define DK_BUTTER lv_color_hex(0xF5E1A4)
#define DK_BUTTER_BG lv_color_hex(0xFBF0D6)
#define DK_BUTTER_INK lv_color_hex(0x7A5A12)
#define DK_SWEAT lv_color_hex(0x7FB6E8)
#define DK_SPARK lv_color_hex(0xF5C64B)

// Night palette
#define DK_NIGHT lv_color_hex(0x221D2B)
#define DK_NIGHT_CARD lv_color_hex(0x332C3F)
#define DK_NIGHT_TEXT lv_color_hex(0xFBF6EF)
#define DK_NIGHT_MUTED lv_color_hex(0xBDB4C9)
#define DK_NIGHT_GOOD lv_color_hex(0x7ED3A8)

LV_FONT_DECLARE(deskling_font_15)
LV_FONT_DECLARE(deskling_font_17)
LV_FONT_DECLARE(deskling_font_19)
LV_FONT_DECLARE(deskling_font_22)
LV_FONT_DECLARE(deskling_font_26)
LV_FONT_DECLARE(deskling_font_34)
LV_FONT_DECLARE(deskling_font_48)
LV_FONT_DECLARE(deskling_font_72)
LV_FONT_DECLARE(deskling_font_200)
LV_FONT_DECLARE(deskling_font_mono_22)

#define DK_FONT_XS (&deskling_font_15)
#define DK_FONT_S (&deskling_font_17)
#define DK_FONT_M (&deskling_font_19)
#define DK_FONT_L (&deskling_font_22)
#define DK_FONT_XL (&deskling_font_26)
#define DK_FONT_H2 (&deskling_font_34)
#define DK_FONT_H1 (&deskling_font_48)
#define DK_FONT_CLOCK (&deskling_font_72)
#define DK_FONT_BIGCLOCK (&deskling_font_200)
#define DK_FONT_MONO (&deskling_font_mono_22)

void dk_theme_init(void);

/** A bare container: no padding, border, scrollbars or background. */
lv_obj_t *dk_box(lv_obj_t *parent);
/** Let children (and their animations) draw up to `px` outside this object. */
void dk_overflow(lv_obj_t *o, int32_t px);
/** A rounded card. */
lv_obj_t *dk_card(lv_obj_t *parent, lv_color_t bg, int32_t radius);
/** Flex helpers. */
void dk_row(lv_obj_t *o, int32_t gap);
void dk_col(lv_obj_t *o, int32_t gap);
lv_obj_t *dk_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text);
/** A pill-shaped tag. */
lv_obj_t *dk_pill(lv_obj_t *parent, lv_color_t bg, const lv_font_t *font, const char *text);
/** A big rounded button. filled = ink background, otherwise outlined. */
lv_obj_t *dk_button(lv_obj_t *parent, const char *text, bool filled, const lv_font_t *font);
/** Section heading in the small caps style. */
lv_obj_t *dk_heading(lv_obj_t *parent, const char *text);
/** A coloured status dot with an optional pulsing ring behind it. */
lv_obj_t *dk_dot(lv_obj_t *parent, int32_t size, lv_color_t color, bool ping);

lv_color_t dk_state_color(int state);
lv_color_t dk_state_pill(int state);
