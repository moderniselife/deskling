// Markdown for chat replies: headings, paragraphs with **bold**, `code`, *emphasis*, ~~strike~~
// and [links](…), bullet and numbered lists, quotes, code blocks, tables and rules. Drawn with
// the Deskling fonts; nothing is clickable, so the chat still scrolls when you drag over it.
#pragma once

#include "lvgl.h"

typedef struct {
    lv_color_t ink;     // body text
    lv_color_t soft;    // quotes, list markers, table lines
    lv_color_t accent;  // links and inline code
    lv_color_t code_bg; // code blocks
} md_style_t;

/**
 * Render `text` into `parent` (made a flex column). `max_w` is the widest a line may get;
 * short replies stay narrow. Returns the container.
 */
lv_obj_t *md_render(lv_obj_t *parent, const char *text, int32_t max_w, const md_style_t *st);

/** The same, but for the whole width (the reader): every block is `max_w` wide. */
lv_obj_t *md_render_wide(lv_obj_t *parent, const char *text, int32_t max_w, const md_style_t *st);
