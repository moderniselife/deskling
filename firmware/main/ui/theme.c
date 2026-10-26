#include <string.h>
#include "theme.h"

#include "anim.h"
#include "model.h"

void dk_theme_init(void)
{
    // Fonts fall back to the built-in Montserrat for LV_SYMBOL_* icons; that's set
    // in the generated font files (tools/gen_fonts.sh) because fonts are const.
}

lv_obj_t *dk_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    // Plain containers must not swallow taps meant for the button they sit in.
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    return o;
}

static void ext_draw_cb(lv_event_t *e)
{
    lv_event_set_ext_draw_size(e, (int32_t)(intptr_t)lv_event_get_user_data(e));
}

void dk_overflow(lv_obj_t *o, int32_t px)
{
    lv_obj_add_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_event_cb(o, ext_draw_cb, LV_EVENT_REFR_EXT_DRAW_SIZE, (void *)(intptr_t)px);
    lv_obj_refresh_ext_draw_size(o);
}

lv_obj_t *dk_card(lv_obj_t *parent, lv_color_t bg, int32_t radius)
{
    lv_obj_t *o = dk_box(parent);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_pad_all(o, 22, 0);
    lv_obj_set_style_clip_corner(o, false, 0);
    return o;
}

void dk_row(lv_obj_t *o, int32_t gap)
{
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(o, gap, 0);
}

void dk_col(lv_obj_t *o, int32_t gap)
{
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(o, gap, 0);
}

void dk_set_text(lv_obj_t *label, const char *text)
{
    const char *cur = lv_label_get_text(label);
    if (cur && text && strcmp(cur, text) == 0) return;
    lv_label_set_text(label, text);
}

uint32_t dk_hash(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

uint32_t dk_hash_str(uint32_t h, const char *s)
{
    return dk_hash(h, s, strlen(s) + 1);
}

lv_obj_t *dk_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text ? text : "");
    return l;
}

lv_obj_t *dk_pill(lv_obj_t *parent, lv_color_t bg, const lv_font_t *font, const char *text)
{
    lv_obj_t *p = dk_card(parent, bg, LV_RADIUS_CIRCLE);
    lv_obj_set_style_pad_hor(p, 14, 0);
    lv_obj_set_style_pad_ver(p, 5, 0);
    dk_label(p, font, DK_INK, text);
    return p;
}

lv_obj_t *dk_button(lv_obj_t *parent, const char *text, bool filled, const lv_font_t *font)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_style_radius(b, 24, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, filled ? DK_INK : DK_CARD, 0);
    lv_obj_set_style_border_width(b, filled ? 0 : 3, 0);
    lv_obj_set_style_border_color(b, DK_INK, 0);
    lv_obj_set_style_pad_hor(b, 22, 0);
    lv_obj_set_style_pad_ver(b, 12, 0);
    lv_obj_set_style_min_height(b, 56, 0);
    lv_obj_t *l = dk_label(b, font, filled ? DK_GROUND : DK_INK, text);
    lv_obj_center(l);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    dk_press_style(b);
    return b;
}

lv_obj_t *dk_heading(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = dk_label(parent, DK_FONT_S, DK_MUTED, text);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    return l;
}

lv_obj_t *dk_dot(lv_obj_t *parent, int32_t size, lv_color_t color, bool ping)
{
    lv_obj_t *wrap = dk_box(parent);
    lv_obj_set_size(wrap, size, size);
    dk_overflow(wrap, size * 2);
    if (ping) {
        lv_obj_t *ring = dk_box(wrap);
        lv_obj_set_size(ring, size, size);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(ring, color, 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
        dk_anim_ping(ring, 1800, 0);
    }
    lv_obj_t *dot = dk_box(wrap);
    lv_obj_set_size(dot, size, size);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, color, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    return wrap;
}

lv_color_t dk_state_color(int state)
{
    switch (state) {
    case DK_WORKING: return DK_PURPLE;
    case DK_WAITING: return lv_color_hex(0xB8860B);
    case DK_NEEDS_YOU: return DK_ALERT_INK;
    case DK_DONE: return DK_GOOD_INK;
    default: return DK_MUTED;
    }
}

lv_color_t dk_state_pill(int state)
{
    switch (state) {
    case DK_WORKING: return DK_LILAC_SOFT;
    case DK_WAITING: return DK_WARM;
    case DK_NEEDS_YOU: return DK_ALERT_PILL;
    case DK_DONE: return DK_GOOD_BG;
    default: return DK_LINE;
    }
}

void dk_one_line(lv_obj_t *label)
{
    // DOTS only cuts text that doesn't fit the label's size: with an automatic height it wraps
    // instead, and a long notification spills over the next row. One line high, it cuts.
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(label, lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)));
}
