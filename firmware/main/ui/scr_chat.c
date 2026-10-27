// One session up close: what it's doing, its recent messages, quick replies and a keyboard.
#include <stdio.h>
#include <string.h>

#include "md.h"
#include "ui_internal.h"

static struct {
    lv_obj_t *scr, *face, *badge_row, *name, *status, *delivery;
    lv_obj_t *right, *bubbles, *quick, *input_row, *ta, *kb, *helpers, *helper_list, *listen, *listen_label, *usage;
    bool listening;
    bool can_message;
    bool follow; // the conversation is pinned to its newest line (you haven't scrolled back)
    char agent_id[72];
    char last_line[16];
    bool keep_place; // the next redraw stays where you are (you opened or closed something)
    lv_obj_t *reader, *reader_title, *reader_body;
    lv_timer_t *reader_timer;
    lv_timer_t *wait_timer; // keeps "28 min" current
    lv_obj_t *wait_note;    // "Up to 28 min" in the waiting card
    char reader_line[24];
    uint8_t reader_asked;
} c;

/* "6 earlier steps" groups you opened, with the steps once they've arrived. */
#define STEPS_OPEN_MAX 6
static struct {
    char id[24];
    int hidden; // how many steps it stood for when they were fetched
    bool have;
    bool failed;    // no answer, even after asking twice
    uint8_t asked;  // times asked
    dk_detail_t d;
} s_open[STEPS_OPEN_MAX];
static int s_open_n;

static md_style_t MD_REPLY; // set in scr_chat_create (the colours aren't compile-time constants)

static void rebuild(bool animate_new);
static void wait_texts(const dk_agent_t *a);
static void wait_tick(lv_timer_t *t);
static uint32_t s_head_sig, s_chat_sig;
static lv_obj_t *s_spacer;

static const char *QUICK[] = {"Yes please", "Continue", "Run the tests", "Wrap up and summarise"};

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_go(SCR_SESSIONS);
}

static void send_text(const char *text)
{
    if (!text || !text[0] || !c.agent_id[0]) return;
    dk_send_message(c.agent_id, text);
    deskling_celebrate(c.face, 900);
}

/* You scrolled: stay where you are unless you came back to the newest line. */
static void bubbles_scrolled(lv_event_t *e)
{
    (void)e;
    c.follow = lv_obj_get_scroll_bottom(c.bubbles) < 80;
}

static void listen_cb(lv_event_t *e)
{
    (void)e;
    if (!c.agent_id[0]) return;
    dk_send_listen(c.agent_id, c.listening ? 0 : 30);
    if (!c.listening) deskling_celebrate(c.face, 900);
}

static void quick_cb(lv_event_t *e)
{
    send_text((const char *)lv_event_get_user_data(e));
}

/* The keyboard covers the bottom of the screen: lift the conversation and the message box above it. */
static void kb_room(bool open)
{
    lv_obj_set_style_pad_bottom(c.right, open ? 22 + 292 : 22, 0);
    if (open) lv_obj_add_flag(c.quick, LV_OBJ_FLAG_HIDDEN);
    else if (c.can_message) lv_obj_remove_flag(c.quick, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(c.right);
    lv_obj_scroll_to_y(c.bubbles, LV_COORD_MAX, LV_ANIM_OFF);
}

static void kb_hide(void)
{
    lv_obj_add_flag(c.kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_state(c.ta, LV_STATE_FOCUSED);
    kb_room(false);
}

static void send_cb(lv_event_t *e)
{
    (void)e;
    send_text(lv_textarea_get_text(c.ta));
    lv_textarea_set_text(c.ta, "");
    kb_hide();
}

static void ta_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        if (lv_obj_has_flag(c.kb, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_remove_flag(c.kb, LV_OBJ_FLAG_HIDDEN);
            dk_anim_drop_in(c.kb, 120, 0);
            kb_room(true);
        }
    } else if (code == LV_EVENT_READY) {
        send_cb(e);
    } else if (code == LV_EVENT_CANCEL) {
        kb_hide();
    }
}

lv_obj_t *scr_chat_create(void)
{
    MD_REPLY = (md_style_t){.ink = DK_INK, .soft = DK_MUTED, .accent = DK_PURPLE, .code_bg = DK_FACE};
    c.scr = lv_obj_create(NULL);
    lv_obj_remove_flag(c.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(c.scr, DK_GROUND, 0);
    lv_obj_set_style_pad_all(c.scr, 28, 0);
    lv_obj_set_style_pad_hor(c.scr, 32, 0);
    dk_row(c.scr, 24);
    lv_obj_set_flex_align(c.scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // ---- left: who this is
    lv_obj_t *left = dk_card(c.scr, DK_LILAC, 36);
    lv_obj_set_size(left, 340, lv_pct(100));
    lv_obj_set_style_pad_hor(left, 26, 0);
    lv_obj_set_style_pad_ver(left, 24, 0);
    dk_col(left, 12);
    // With helpers and listening on, the panel can outgrow the screen: let it scroll.
    lv_obj_add_flag(left, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(left, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(left, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *back = lv_button_create(left);
    lv_obj_remove_style_all(back);
    lv_obj_set_style_bg_color(back, DK_CARD, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(back, 18, 0);
    lv_obj_set_height(back, 52);
    dk_row(back, 8);
    dk_label(back, &lv_font_montserrat_20, DK_INK, LV_SYMBOL_LEFT);
    dk_label(back, DK_FONT_S, DK_INK, "Sessions");
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    dk_press_style(back);

    lv_obj_t *fw = dk_box(left);
    lv_obj_set_size(fw, lv_pct(100), 150);
    dk_overflow(fw, 60);
    c.face = deskling_create(fw, 160, DK_FACE);
    lv_obj_align(c.face, LV_ALIGN_CENTER, 0, 0);

    c.badge_row = dk_box(left);
    dk_row(c.badge_row, 8);
    c.name = dk_label(left, DK_FONT_H2, DK_INK, "");
    lv_obj_set_width(c.name, lv_pct(100));
    dk_one_line(c.name);
    c.status = dk_label(left, DK_FONT_M, DK_INK_SOFT, "");
    lv_obj_set_width(c.status, lv_pct(100));
    lv_label_set_long_mode(c.status, LV_LABEL_LONG_MODE_WRAP);
    c.usage = dk_label(left, DK_FONT_XS, DK_MUTED, "");
    lv_obj_set_width(c.usage, lv_pct(100));

    lv_obj_t *info = dk_card(left, DK_CARD, 22);
    lv_obj_set_width(info, lv_pct(100));
    lv_obj_set_style_pad_all(info, 16, 0);
    dk_col(info, 4);
    dk_heading(info, "DELIVERY");
    c.delivery = dk_label(info, DK_FONT_S, DK_INK, "");
    lv_obj_set_width(c.delivery, lv_pct(100));
    lv_label_set_long_mode(c.delivery, LV_LABEL_LONG_MODE_WRAP);
    // "Wait for my reply": when it finishes, it waits for your next message from here.
    c.listen = dk_button(info, "Wait for my reply", false, DK_FONT_S);
    lv_obj_set_width(c.listen, lv_pct(100));
    lv_obj_set_style_min_height(c.listen, 48, 0);
    lv_obj_set_style_margin_top(c.listen, 8, 0);
    c.listen_label = lv_obj_get_child(c.listen, 0);
    lv_obj_add_event_cb(c.listen, listen_cb, LV_EVENT_CLICKED, NULL);
    c.wait_timer = lv_timer_create(wait_tick, 15000, NULL);

    // Helpers: the subagents this session has handed work to.
    c.helpers = dk_card(left, DK_CARD, 22);
    lv_obj_set_width(c.helpers, lv_pct(100));
    lv_obj_set_style_pad_all(c.helpers, 16, 0);
    dk_col(c.helpers, 8);
    dk_heading(c.helpers, "HELPERS");
    c.helper_list = dk_box(c.helpers);
    lv_obj_set_width(c.helper_list, lv_pct(100));
    dk_col(c.helper_list, 8);
    lv_obj_add_flag(c.helpers, LV_OBJ_FLAG_HIDDEN);

    // ---- right: the conversation
    lv_obj_t *right = c.right = dk_card(c.scr, DK_CARD, 36);
    lv_obj_set_height(right, lv_pct(100));
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_style_pad_hor(right, 26, 0);
    lv_obj_set_style_pad_ver(right, 22, 0);
    dk_col(right, 14);

    c.bubbles = dk_box(right);
    lv_obj_set_width(c.bubbles, lv_pct(100));
    lv_obj_set_flex_grow(c.bubbles, 1);
    dk_col(c.bubbles, 12);
    // Start-aligned (end-aligned content that overflows can't be scrolled back to); a growing
    // spacer at the top keeps a short conversation sitting at the bottom.
    lv_obj_set_flex_align(c.bubbles, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    // Bubbles are plain labels, so the list itself has to take the press or a drag can't scroll it.
    lv_obj_add_flag(c.bubbles, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(c.bubbles, LV_DIR_VER);
    lv_obj_add_event_cb(c.bubbles, bubbles_scrolled, LV_EVENT_SCROLL_END, NULL);
    // Pushes a short conversation down to the input, like a messaging app.
    s_spacer = dk_box(c.bubbles);
    lv_obj_set_size(s_spacer, 1, 0);
    lv_obj_set_flex_grow(s_spacer, 1);
    lv_obj_set_scrollbar_mode(c.bubbles, LV_SCROLLBAR_MODE_OFF);

    c.quick = dk_box(right);
    lv_obj_set_width(c.quick, lv_pct(100));
    lv_obj_set_flex_flow(c.quick, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(c.quick, 10, 0);
    lv_obj_set_style_pad_row(c.quick, 10, 0);
    for (size_t i = 0; i < sizeof(QUICK) / sizeof(QUICK[0]); i++) {
        lv_obj_t *b = dk_button(c.quick, QUICK[i], false, DK_FONT_S);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_height(b, 54);
        lv_obj_add_event_cb(b, quick_cb, LV_EVENT_CLICKED, (void *)QUICK[i]);
    }

    c.input_row = dk_box(right);
    lv_obj_set_width(c.input_row, lv_pct(100));
    dk_row(c.input_row, 12);
    c.ta = lv_textarea_create(c.input_row);
    lv_textarea_set_one_line(c.ta, true);
    lv_textarea_set_placeholder_text(c.ta, "Message this session…");
    lv_obj_set_height(c.ta, 64);
    lv_obj_set_flex_grow(c.ta, 1);
    lv_obj_set_style_radius(c.ta, 22, 0);
    lv_obj_set_style_border_width(c.ta, 3, 0);
    lv_obj_set_style_border_color(c.ta, lv_color_hex(0xE4DCCF), 0);
    lv_obj_set_style_border_color(c.ta, DK_PURPLE, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(c.ta, DK_GROUND, 0);
    lv_obj_set_style_text_font(c.ta, DK_FONT_M, 0);
    lv_obj_set_style_text_color(c.ta, DK_INK, 0);
    lv_obj_set_style_pad_hor(c.ta, 20, 0);
    lv_obj_set_style_pad_ver(c.ta, 18, 0);
    lv_obj_add_event_cb(c.ta, ta_cb, LV_EVENT_ALL, NULL);
    lv_obj_t *send = lv_button_create(c.input_row);
    lv_obj_remove_style_all(send);
    lv_obj_set_size(send, 64, 64);
    lv_obj_set_style_radius(send, 22, 0);
    lv_obj_set_style_bg_color(send, DK_INK, 0);
    lv_obj_set_style_bg_opa(send, LV_OPA_COVER, 0);
    lv_obj_t *sl = dk_label(send, &lv_font_montserrat_28, DK_GROUND, LV_SYMBOL_RIGHT);
    lv_obj_center(sl);
    lv_obj_add_event_cb(send, send_cb, LV_EVENT_CLICKED, NULL);
    dk_press_style(send);

    // Keyboard floats over the bottom of the screen while typing.
    c.kb = lv_keyboard_create(c.scr);
    lv_obj_add_flag(c.kb, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(c.kb, DK_W, 320);
    lv_obj_align(c.kb, LV_ALIGN_BOTTOM_MID, 0, 28);
    lv_keyboard_set_textarea(c.kb, c.ta);
    lv_obj_set_style_bg_color(c.kb, lv_color_hex(0xEDE6DA), 0);
    lv_obj_set_style_text_font(c.kb, DK_FONT_L, LV_PART_ITEMS);
    lv_obj_set_style_radius(c.kb, 14, LV_PART_ITEMS);
    lv_obj_add_event_cb(c.kb, ta_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(c.kb, ta_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(c.kb, LV_OBJ_FLAG_HIDDEN);
    return c.scr;
}

static void reader_open(const char *line_id);

/* A button that acts on a chat line keeps its own copy of the line's id: the model under it
   is rewritten by every snapshot. */
static void id_free_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static void attach_id(lv_obj_t *o, lv_event_cb_t cb, const char *id)
{
    size_t n = strlen(id) + 1;
    char *copy = lv_malloc(n);
    if (!copy) return;
    memcpy(copy, id, n);
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, copy);
    lv_obj_add_event_cb(o, id_free_cb, LV_EVENT_DELETE, copy);
}

static void show_all_cb(lv_event_t *e)
{
    reader_open((const char *)lv_event_get_user_data(e));
}

static lv_obj_t *bubble(const char *text, bool mine, const char *meta, const char *image, const char *quote, const char *more_id)
{
    lv_obj_t *wrap = dk_box(c.bubbles);
    lv_obj_set_width(wrap, lv_pct(100));
    dk_col(wrap, 4);
    lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_START, mine ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, mine ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START);
    lv_obj_t *b = dk_card(wrap, mine ? DK_INK : DK_LINE, 24);
    lv_obj_set_style_pad_hor(b, 18, 0);
    lv_obj_set_style_pad_ver(b, 14, 0);
    if ((image && image[0]) || (quote && quote[0]) || !mine || more_id) dk_col(b, 10);
    if (quote && quote[0]) {
        // What you replied to: a quiet block with a bar down its side, like a reply in the app.
        lv_obj_t *q = dk_box(b);
        lv_obj_set_style_border_side(q, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(q, 4, 0);
        lv_obj_set_style_border_color(q, DK_LILAC, 0);
        lv_obj_set_style_pad_left(q, 12, 0);
        lv_obj_t *ql = dk_label(q, DK_FONT_S, mine ? DK_LILAC_SOFT : DK_MUTED, quote);
        lv_obj_set_width(ql, 600);
        lv_obj_set_height(ql, LV_SIZE_CONTENT);
        lv_obj_set_style_max_height(ql, 96, 0);
        lv_label_set_long_mode(ql, LV_LABEL_LONG_MODE_DOTS);
    }
    if (image && image[0]) {
        bool failed = false;
        const lv_image_dsc_t *img = ui_image_get(image, &failed);
        if (img) {
            lv_obj_t *pic = lv_image_create(b);
            lv_image_set_src(pic, img);
            lv_obj_set_style_radius(pic, 14, 0);
            lv_obj_set_style_clip_corner(pic, true, 0);
        } else {
            // Room for the picture while it downloads, so the chat doesn't jump.
            lv_obj_t *ph = dk_card(b, mine ? DK_INK_SOFT : DK_CARD, 14);
            lv_obj_set_size(ph, 320, 200);
            lv_obj_t *pl = dk_label(ph, DK_FONT_S, mine ? DK_GROUND : DK_MUTED, failed ? "Couldn't load this picture" : "Loading the picture...");
            lv_obj_center(pl);
        }
    }
    if (text && text[0] && mine) {
        lv_obj_t *l = dk_label(b, DK_FONT_M, DK_GROUND, text);
        lv_obj_set_style_max_width(l, 680, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    } else if (text && text[0]) {
        // Replies are Markdown: headings, bold, code, lists and tables drawn as such.
        md_render(b, text, 680, &MD_REPLY);
    }
    if (more_id) {
        // The reply goes on: open it whole in the reader.
        lv_obj_t *btn = lv_button_create(b);
        lv_obj_remove_style_all(btn);
        lv_obj_set_style_bg_color(btn, DK_CARD, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_hor(btn, 18, 0);
        lv_obj_set_height(btn, 48);
        dk_row(btn, 8);
        dk_label(btn, DK_FONT_S, DK_PURPLE, "Show all");
        dk_label(btn, &lv_font_montserrat_20, DK_PURPLE, LV_SYMBOL_RIGHT);
        attach_id(btn, show_all_cb, more_id);
        dk_press_style(btn);
    }
    if (meta) dk_label(wrap, DK_FONT_XS, DK_MUTED, meta);
    return wrap;
}

/* A step it took: a quiet line with a dot, like the app's list of steps. */
static lv_obj_t *step_row(const char *text, bool thought)
{
    lv_obj_t *row = dk_box(c.bubbles);
    lv_obj_set_width(row, lv_pct(100));
    dk_row(row, 10);
    lv_obj_set_style_pad_left(row, 6, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *dot = dk_box(row);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, thought ? DK_PEACH : DK_LILAC, 0);
    lv_obj_t *l = dk_label(row, DK_FONT_S, thought ? DK_INK_SOFT : DK_MUTED, text);
    lv_obj_set_flex_grow(l, 1);
    dk_one_line(l);
    return row;
}

static const dk_chat_t *find_line(const char *id);

static int open_find(const char *id)
{
    for (int i = 0; i < s_open_n; i++)
        if (strcmp(s_open[i].id, id) == 0) return i;
    return -1;
}

static void open_drop(int i)
{
    dk_detail_free(&s_open[i].d);
    s_open[i] = s_open[--s_open_n];
    memset(&s_open[s_open_n], 0, sizeof(s_open[0]));
}

static void open_clear(void)
{
    while (s_open_n) open_drop(0);
}

static void redraw_here(void)
{
    c.keep_place = true;
    s_chat_sig = 0;
    rebuild(false);
}

static void steps_timeout(lv_timer_t *t)
{
    char *id = lv_timer_get_user_data(t);
    int i = open_find(id);
    if (i >= 0 && !s_open[i].have && !s_open[i].failed) {
        if (s_open[i].asked < 2) {
            s_open[i].asked++;
            dk_send_detail(c.agent_id, id); // once more: the answer may have been lost on a reconnect
            lv_timer_t *again = lv_timer_create(steps_timeout, 6000, id);
            lv_timer_set_repeat_count(again, 1);
            return;
        }
        s_open[i].failed = true;
        redraw_here();
    }
    lv_free(id);
}

static void ask_steps(int i)
{
    s_open[i].asked = 1;
    dk_send_detail(c.agent_id, s_open[i].id);
    size_t n = strlen(s_open[i].id) + 1;
    char *id = lv_malloc(n);
    if (!id) return;
    memcpy(id, s_open[i].id, n);
    lv_timer_t *t = lv_timer_create(steps_timeout, 6000, id);
    lv_timer_set_repeat_count(t, 1);
}

static void steps_cb(lv_event_t *e)
{
    const char *id = lv_event_get_user_data(e);
    const dk_chat_t *line = find_line(id);
    if (!line) return;
    int i = open_find(line->id);
    if (i >= 0) {
        open_drop(i); // close it
    } else {
        if (s_open_n == STEPS_OPEN_MAX) open_drop(0);
        i = s_open_n++;
        memset(&s_open[i], 0, sizeof(s_open[i]));
        snprintf(s_open[i].id, sizeof(s_open[i].id), "%s", line->id);
        s_open[i].hidden = line->hidden;
        ask_steps(i);
    }
    redraw_here();
}

/* "6 earlier steps": tap to list them here, tap again to fold them away. */
static lv_obj_t *steps_group(const dk_chat_t *line)
{
    int i = open_find(line->id);
    // More steps joined the group since it was opened: fetch the longer list.
    if (i >= 0 && s_open[i].have && s_open[i].hidden != line->hidden) {
        dk_detail_free(&s_open[i].d);
        s_open[i].have = false;
        s_open[i].hidden = line->hidden;
        ask_steps(i);
    }
    lv_obj_t *wrap = dk_box(c.bubbles);
    lv_obj_set_width(wrap, lv_pct(100));
    dk_col(wrap, 10);
    lv_obj_t *row = lv_button_create(wrap);
    lv_obj_remove_style_all(row);
    lv_obj_set_style_bg_color(row, DK_LINE, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(row, 16, 0);
    lv_obj_set_height(row, 44);
    dk_row(row, 10);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    dk_label(row, &lv_font_montserrat_20, DK_PURPLE, i >= 0 ? LV_SYMBOL_DOWN : LV_SYMBOL_RIGHT);
    dk_label(row, DK_FONT_S, DK_INK_SOFT, line->text);
    attach_id(row, steps_cb, line->id);
    dk_press_style(row);
    if (i < 0) return wrap;

    lv_obj_t *list = dk_box(wrap);
    lv_obj_set_width(list, lv_pct(100));
    dk_col(list, 12);
    lv_obj_set_style_pad_left(list, 14, 0);
    lv_obj_set_style_border_side(list, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(list, 3, 0);
    lv_obj_set_style_border_color(list, DK_LILAC, 0);
    if (s_open[i].failed) {
        dk_label(list, DK_FONT_S, DK_ALERT_INK, "Couldn't load the steps. Tap above to close them, then open them again.");
    } else if (!s_open[i].have) {
        dk_label(list, DK_FONT_S, DK_MUTED, "Loading the steps...");
    } else if (!s_open[i].d.steps_n) {
        dk_label(list, DK_FONT_S, DK_MUTED, "These steps are no longer kept.");
    } else {
        for (int k = 0; k < s_open[i].d.steps_n; k++) {
            lv_obj_t *r = dk_box(list);
            lv_obj_set_width(r, lv_pct(100));
            dk_row(r, 10);
            lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_t *dot = dk_box(r);
            lv_obj_set_size(dot, 8, 8);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(dot, s_open[i].d.steps[k].thought ? DK_PEACH : DK_LILAC, 0);
            lv_obj_t *l = dk_label(r, DK_FONT_S, s_open[i].d.steps[k].thought ? DK_INK_SOFT : DK_MUTED, s_open[i].d.steps[k].text);
            lv_obj_set_flex_grow(l, 1);
            dk_one_line(l);
        }
    }
    return wrap;
}

// ------------------------------------------------------------ the reader

static void reader_close(void)
{
    if (c.reader_timer) {
        lv_timer_delete(c.reader_timer);
        c.reader_timer = NULL;
    }
    c.reader_line[0] = '\0';
    if (c.reader) lv_obj_add_flag(c.reader, LV_OBJ_FLAG_HIDDEN);
}

static void reader_close_cb(lv_event_t *e)
{
    (void)e;
    reader_close();
}

static void reader_fill(const char *text, bool whole)
{
    lv_obj_clean(c.reader_body);
    if (!whole) {
        lv_obj_t *n = dk_label(c.reader_body, DK_FONT_S, DK_ALERT_INK, "Couldn't fetch the rest of this reply. Here's the start of it.");
        lv_obj_set_width(n, lv_pct(100));
        lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_WRAP);
    }
    md_render_wide(c.reader_body, text, 1060, &MD_REPLY);
    lv_obj_scroll_to_y(c.reader_body, 0, LV_ANIM_OFF);
}

static const dk_chat_t *find_line(const char *id)
{
    const dk_agent_t *a = dk_model_find_agent(g_model, c.agent_id);
    for (int i = 0; a && i < a->chat_n; i++)
        if (strcmp(a->chat[i].id, id) == 0) return &a->chat[i];
    return NULL;
}

static void reader_timeout(lv_timer_t *t)
{
    (void)t;
    c.reader_timer = NULL;
    if (!c.reader_line[0]) return;
    if (c.reader_asked < 2) {
        // Ask once more: the answer may have been lost on a reconnect.
        c.reader_asked++;
        dk_send_detail(c.agent_id, c.reader_line);
        c.reader_timer = lv_timer_create(reader_timeout, 6000, NULL);
        lv_timer_set_repeat_count(c.reader_timer, 1);
        return;
    }
    const dk_chat_t *line = find_line(c.reader_line);
    reader_fill(line ? line->text : "", false);
}

static void reader_open(const char *line_id)
{
    if (!c.reader) {
        c.reader = dk_box(c.scr);
        lv_obj_add_flag(c.reader, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_pos(c.reader, -32, -28); // over the screen's padding too
        lv_obj_set_size(c.reader, DK_W, DK_H);
        lv_obj_set_style_bg_color(c.reader, DK_GROUND, 0);
        lv_obj_set_style_bg_opa(c.reader, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(c.reader, 32, 0);
        lv_obj_set_style_pad_ver(c.reader, 28, 0);
        dk_col(c.reader, 18);
        lv_obj_t *head = dk_box(c.reader);
        lv_obj_set_width(head, lv_pct(100));
        dk_row(head, 18);
        lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *back = lv_button_create(head);
        lv_obj_remove_style_all(back);
        lv_obj_set_style_bg_color(back, DK_LILAC, 0);
        lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_hor(back, 18, 0);
        lv_obj_set_height(back, 52);
        dk_row(back, 8);
        dk_label(back, &lv_font_montserrat_20, DK_INK, LV_SYMBOL_LEFT);
        dk_label(back, DK_FONT_S, DK_INK, "Back to the chat");
        lv_obj_add_event_cb(back, reader_close_cb, LV_EVENT_CLICKED, NULL);
        dk_press_style(back);
        c.reader_title = dk_label(head, DK_FONT_L, DK_INK, "");
        lv_obj_set_flex_grow(c.reader_title, 1);
        dk_one_line(c.reader_title);
        lv_obj_t *page = dk_card(c.reader, DK_CARD, 28);
        lv_obj_set_width(page, lv_pct(100));
        lv_obj_set_flex_grow(page, 1);
        lv_obj_set_style_pad_all(page, 0, 0);
        c.reader_body = dk_box(page);
        lv_obj_set_size(c.reader_body, lv_pct(100), lv_pct(100));
        lv_obj_set_style_pad_hor(c.reader_body, 40, 0);
        lv_obj_set_style_pad_ver(c.reader_body, 30, 0);
        dk_col(c.reader_body, 14);
        lv_obj_add_flag(c.reader_body, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ELASTIC);
        lv_obj_set_scroll_dir(c.reader_body, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(c.reader_body, LV_SCROLLBAR_MODE_ACTIVE);
    }
    const dk_agent_t *a = dk_model_find_agent(g_model, c.agent_id);
    char title[96];
    snprintf(title, sizeof(title), "%s's reply%s%s", a && a->source != DK_SRC_CLAUDE ? dk_source_label(a->source) : "Claude", a ? " in " : "", a ? a->name : "");
    dk_set_text(c.reader_title, title);
    snprintf(c.reader_line, sizeof(c.reader_line), "%s", line_id);
    lv_obj_clean(c.reader_body);
    dk_label(c.reader_body, DK_FONT_M, DK_MUTED, "Loading the whole reply...");
    lv_obj_remove_flag(c.reader, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(c.reader);
    dk_anim_rise(c.reader, 0);
    if (c.reader_timer) lv_timer_delete(c.reader_timer);
    c.reader_timer = lv_timer_create(reader_timeout, 6000, NULL);
    lv_timer_set_repeat_count(c.reader_timer, 1);
    c.reader_asked = 1;
    dk_send_detail(c.agent_id, line_id);
}

bool scr_chat_back(void)
{
    if (!c.reader || lv_obj_has_flag(c.reader, LV_OBJ_FLAG_HIDDEN)) return false;
    reader_close();
    return true;
}

void scr_chat_detail(dk_detail_t *d)
{
    if (!c.scr || strcmp(d->agent_id, c.agent_id) != 0) return;
    if (c.reader_line[0] && strcmp(d->line_id, c.reader_line) == 0) {
        if (c.reader_timer) {
            lv_timer_delete(c.reader_timer);
            c.reader_timer = NULL;
        }
        const dk_chat_t *line = find_line(d->line_id);
        if (d->text && d->text[0]) reader_fill(d->text, true);
        else reader_fill(line ? line->text : "", false);
        return;
    }
    int i = open_find(d->line_id);
    if (i < 0 || s_open[i].have) return;
    s_open[i].d = *d; // the steps are ours now
    memset(d, 0, sizeof(*d));
    s_open[i].have = true;
    redraw_here();
}

/* Claude is waiting for your reply: say so where you're looking, above the box you'd type in. */
static lv_obj_t *waiting_card(void)
{
    lv_obj_t *b = dk_card(c.bubbles, DK_BUTTER_BG, 24);
    lv_obj_set_width(b, lv_pct(100));
    lv_obj_set_style_pad_hor(b, 20, 0);
    lv_obj_set_style_pad_ver(b, 14, 0);
    dk_row(b, 14);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *dot = dk_box(b);
    lv_obj_set_size(dot, 14, 14);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, DK_SPARK, 0);
    dk_anim_breathe_opa(dot, LV_OPA_40, LV_OPA_COVER, 1600, 0);
    lv_obj_t *col = dk_box(b);
    lv_obj_set_flex_grow(col, 1);
    dk_col(col, 2);
    const dk_agent_t *a = dk_model_find_agent(g_model, c.agent_id);
    char t[64];
    snprintf(t, sizeof(t), "%s is waiting for your reply", a && a->source != DK_SRC_CLAUDE ? dk_source_label(a->source) : "Claude");
    dk_label(col, DK_FONT_M, DK_BUTTER_INK, t);
    c.wait_note = dk_label(col, DK_FONT_S, DK_BUTTER_INK, "");
    lv_obj_set_width(c.wait_note, lv_pct(100));
    lv_label_set_long_mode(c.wait_note, LV_LABEL_LONG_MODE_WRAP);
    if (a) wait_texts(a);
    return b;
}

static lv_obj_t *typing(void)
{
    lv_obj_t *b = dk_card(c.bubbles, DK_LINE, 24);
    lv_obj_set_style_pad_hor(b, 20, 0);
    lv_obj_set_style_pad_ver(b, 16, 0);
    dk_row(b, 6);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *d = dk_box(b);
        lv_obj_set_size(d, 10, 10);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, DK_INK, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        dk_anim_bob(d, 7, 600, i * 150);
    }
    dk_anim_pop(b, 0);
    return b;
}


static bool near_bottom(void)
{
    return lv_obj_get_scroll_bottom(c.bubbles) < 80;
}

/* Who it is and what it's doing. Live activity changes this every few seconds; the bubbles
   below are left alone unless the conversation itself changed, so you can scroll back and read. */
static void rebuild_head(const dk_agent_t *a)
{
    uint32_t sig = dk_hash_str(DK_HASH0, c.agent_id);
    if (a) {
        sig = dk_hash_str(sig, a->name);
        sig = dk_hash_str(sig, a->host);
        sig = dk_hash_str(sig, a->task);
        sig = dk_hash(sig, &a->state, sizeof(a->state));
        sig = dk_hash(sig, &a->source, sizeof(a->source));
        sig = dk_hash(sig, &a->channel, sizeof(a->channel));
        sig = dk_hash(sig, &a->can_message, sizeof(a->can_message));
        sig = dk_hash(sig, a->helpers, sizeof(a->helpers[0]) * a->helpers_n);
        sig = dk_hash(sig, &a->helpers_n, sizeof(a->helpers_n));
        sig = dk_hash(sig, &a->listening, sizeof(a->listening));
        sig = dk_hash(sig, &a->holding, sizeof(a->holding));
        sig = dk_hash_str(sig, a->model);
        sig = dk_hash(sig, &a->context, sizeof(a->context));
    }
    if (sig == s_head_sig) return;
    s_head_sig = sig;
    lv_obj_clean(c.badge_row);
    if (!a) {
        lv_label_set_text(c.name, "Session ended");
        lv_label_set_text(c.status, "This session isn't running any more.");
        lv_label_set_text(c.delivery, "");
        deskling_set_mood(c.face, DK_MOOD_SLEEPY);
        lv_obj_add_flag(c.quick, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(c.input_row, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    ui_source_badge(c.badge_row, a->source);
    dk_label(c.badge_row, DK_FONT_S, DK_INK, a->host);
    dk_set_text(c.name, a->name);
    char st[160];
    snprintf(st, sizeof(st), "%s%s%s", dk_state_label(a->state), a->task[0] ? " · " : "", a->task);
    dk_set_text(c.status, st);
    char mc[80] = "";
    if (a->model[0]) {
        // "claude-opus-5-5" -> "Opus 5.5"
        const char *m = strncmp(a->model, "claude-", 7) == 0 ? a->model + 7 : a->model;
        char fam[24] = "";
        int maj = 0, min = 0;
        if (sscanf(m, "%23[a-z]-%d-%d", fam, &maj, &min) >= 2) {
            fam[0] = (char)(fam[0] >= 'a' && fam[0] <= 'z' ? fam[0] - 32 : fam[0]);
            if (min) snprintf(mc, sizeof(mc), "%s %d.%d", fam, maj, min);
            else snprintf(mc, sizeof(mc), "%s %d", fam, maj);
        } else {
            snprintf(mc, sizeof(mc), "%s", m);
        }
    }
    if (a->context > 0) {
        size_t n = strlen(mc);
        snprintf(mc + n, sizeof(mc) - n, "%s%dk tokens in context", mc[0] ? " · " : "", (int)((a->context + 500) / 1000));
    }
    dk_set_text(c.usage, mc);
    c.listening = a->listening;
    c.can_message = a->can_message;
    wait_texts(a);
    if (a->can_message && !a->channel && a->source == DK_SRC_CLAUDE) {
        lv_obj_remove_flag(c.listen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(c.listen, a->listening ? DK_INK : DK_CARD, 0);
        lv_obj_set_style_border_width(c.listen, a->listening ? 0 : 3, 0);
        lv_obj_set_style_text_color(c.listen_label, a->listening ? DK_GROUND : DK_INK, 0);
    } else {
        lv_obj_add_flag(c.listen, LV_OBJ_FLAG_HIDDEN);
    }
    deskling_set_outfit(c.face, g_model->quests.outfits);
    dk_mood_t mood = ui_mood_for(a);
    if (deskling_get_mood(c.face) != mood && deskling_get_mood(c.face) != DK_MOOD_HAPPY) deskling_set_mood(c.face, mood);

    lv_obj_clean(c.helper_list);
    if (a->helpers_n) {
        lv_obj_remove_flag(c.helpers, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < a->helpers_n; i++) {
            const dk_helper_t *h = &a->helpers[i];
            lv_obj_t *row = dk_box(c.helper_list);
            lv_obj_set_width(row, lv_pct(100));
            dk_row(row, 10);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            lv_obj_t *dot = dk_box(row);
            lv_obj_set_size(dot, 12, 12);
            lv_obj_set_style_margin_top(dot, 5, 0);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(dot, h->done ? DK_GOOD_INK : DK_PURPLE, 0);
            if (!h->done) dk_anim_breathe_opa(dot, LV_OPA_40, LV_OPA_COVER, 1400, i * 200);
            lv_obj_t *col = dk_box(row);
            lv_obj_set_flex_grow(col, 1);
            dk_col(col, 0);
            lv_obj_t *n = dk_label(col, DK_FONT_S, DK_INK, h->name);
            lv_obj_set_width(n, lv_pct(100));
            dk_one_line(n);
            lv_obj_t *t = dk_label(col, DK_FONT_XS, DK_MUTED, h->done ? "Finished" : (h->task[0] ? h->task : "Working"));
            lv_obj_set_width(t, lv_pct(100));
            dk_one_line(t);
        }
    } else {
        lv_obj_add_flag(c.helpers, LV_OBJ_FLAG_HIDDEN);
    }

    if (a->can_message) {
        lv_obj_remove_flag(c.quick, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(c.input_row, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(c.quick, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(c.input_row, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Minutes of "Wait for my reply" left, rounded up. */
static int wait_minutes(const dk_agent_t *a)
{
    int64_t left = a->listen_until - (ui_now_ms() + ui_relay_offset_ms());
    return left > 0 ? (int)((left + 59999) / 60000) : 0;
}

/* The words for "Wait for my reply": the button, what it means right now, and the card in the chat. */
static void wait_texts(const dk_agent_t *a)
{
    int min = wait_minutes(a);
    char btn[48], del[200];
    const char *who = a->source == DK_SRC_CLAUDE ? "Claude" : dk_source_label(a->source);
    if (!a->listening || !min) {
        snprintf(btn, sizeof(btn), "Wait for my reply");
        snprintf(del, sizeof(del), "Messages arrive when %s finishes its current step, or with your next prompt. \"Wait for my reply\" keeps it open for you when it finishes.", who);
    } else if (a->holding) {
        snprintf(btn, sizeof(btn), "Stop waiting");
        snprintf(del, sizeof(del), "%s is waiting for your reply. Anything you send goes straight in.", who);
    } else if (a->state == DK_WORKING || a->state == DK_NEEDS_YOU) {
        snprintf(btn, sizeof(btn), "On for %d min · tap to stop", min);
        snprintf(del, sizeof(del), "When %s finishes, it waits for your reply here instead of stopping.", who);
    } else {
        snprintf(btn, sizeof(btn), "On for %d min · tap to stop", min);
        snprintf(del, sizeof(del), "%s has already stopped, so this starts the next time it finishes. A message now reaches it with your next prompt.", who);
    }
    dk_set_text(c.listen_label, btn);
    dk_set_text(c.delivery, a->channel ? "Live channel: messages arrive straight away." : a->can_message ? del : "This tool can't take messages from Deskling yet.");
    if (c.wait_note) {
        char note[96];
        snprintf(note, sizeof(note), "Type below or tap a quick reply. It waits up to %d min more.", min);
        dk_set_text(c.wait_note, note);
    }
}

static void wait_tick(lv_timer_t *t)
{
    (void)t;
    const dk_agent_t *a = dk_model_find_agent(g_model, c.agent_id);
    if (a && a->listening && lv_screen_active() == c.scr) wait_texts(a);
}

static const char *chat_meta(const dk_agent_t *a, const dk_chat_t *line)
{
    if (line->from_you) {
        switch (line->status) {
        case DK_CHAT_QUEUED: return "Queued";
        case DK_CHAT_TYPED: return "From your computer";
        default: return "Delivered";
        }
    }
    return a->source == DK_SRC_CLAUDE ? "Claude" : dk_source_label(a->source);
}

/* The conversation is drawn as rows, one per chat line plus the "typing" or "waiting" card at
   the end. Each row remembers what it showed (a hash), so a snapshot only rebuilds the rows that
   changed and the ones after them: a new step or a growing reply touches the last row or two,
   not thirty Markdown bubbles. */
#define ROWS_MAX (DK_MAX_CHAT + 4)
enum { ROW_TYPING = 1, ROW_WAITING, ROW_EMPTY_PROMPT, ROW_EMPTY_REPLY, ROW_EMPTY };
static struct {
    uint32_t key, hash;
    lv_obj_t *obj;
} s_rows[ROWS_MAX];
static int s_rows_n;

static void rows_drop_from(int from)
{
    for (int i = s_rows_n - 1; i >= from; i--) {
        if (s_rows[i].key == ROW_WAITING) c.wait_note = NULL;
        lv_obj_delete(s_rows[i].obj);
    }
    if (from < s_rows_n) s_rows_n = from;
}

static void rows_clear(void)
{
    rows_drop_from(0);
}

static uint32_t line_key(const dk_chat_t *l)
{
    uint32_t k = dk_hash_str(DK_HASH0, l->id);
    return k <= ROW_EMPTY ? k + 100 : k;
}

static uint32_t line_hash(const dk_agent_t *a, const dk_chat_t *l)
{
    uint32_t h = dk_hash_str(DK_HASH0, l->text);
    h = dk_hash_str(h, l->quote);
    h = dk_hash_str(h, l->image);
    int pic = l->image[0] ? ui_image_state(l->image) : 0;
    h = dk_hash(h, &pic, sizeof(pic));
    h = dk_hash(h, &l->more, sizeof(l->more));
    h = dk_hash(h, &l->hidden, sizeof(l->hidden));
    h = dk_hash(h, &l->step, sizeof(l->step));
    h = dk_hash(h, &l->thought, sizeof(l->thought));
    h = dk_hash(h, &l->from_you, sizeof(l->from_you));
    h = dk_hash_str(h, chat_meta(a, l));
    if (l->hidden > 0) {
        int i = open_find(l->id);
        int st[4] = {i >= 0, i >= 0 && s_open[i].have, i >= 0 ? s_open[i].d.steps_n : 0, i >= 0 && s_open[i].failed};
        h = dk_hash(h, st, sizeof(st));
    }
    return h;
}

static lv_obj_t *line_row(const dk_agent_t *a, const dk_chat_t *line)
{
    if (line->hidden > 0) return steps_group(line);
    if (line->step) return step_row(line->text, line->thought);
    return bubble(line->text, line->from_you, chat_meta(a, line), line->image, line->quote, line->more && !line->from_you ? line->id : NULL);
}

static void rebuild_chat(const dk_agent_t *a, bool animate_new)
{
    const dk_chat_t *tail = a && a->chat_n ? &a->chat[a->chat_n - 1] : NULL;
    // "Typing" only while it's actually busy: not on a session that's gone quiet.
    // (The session may be gone: the chat stays open on what was last seen.)
    bool recent = a && (!g_model->now || !a->updated || g_model->now - a->updated < 3 * 60 * 1000);
    bool typing_on = tail && recent && (tail->step || (tail->from_you && tail->status != DK_CHAT_QUEUED)) && a->state == DK_WORKING;
    bool waiting = a && !typing_on && a->holding && a->listening;

    // What the rows should be.
    uint32_t keys[ROWS_MAX], hashes[ROWS_MAX];
    const dk_chat_t *lines[ROWS_MAX];
    int n = 0;
    if (a && a->chat_n == 0) {
        if (a->last_prompt[0]) keys[n] = ROW_EMPTY_PROMPT, hashes[n] = dk_hash_str(DK_HASH0, a->last_prompt), lines[n++] = NULL;
        if (a->last_message[0]) keys[n] = ROW_EMPTY_REPLY, hashes[n] = dk_hash_str(DK_HASH0, a->last_message), lines[n++] = NULL;
        if (!n) keys[n] = ROW_EMPTY, hashes[n] = 0, lines[n++] = NULL;
    }
    for (int i = 0; a && i < a->chat_n && n < ROWS_MAX - 1; i++) {
        keys[n] = line_key(&a->chat[i]);
        hashes[n] = line_hash(a, &a->chat[i]);
        lines[n++] = &a->chat[i];
    }
    if (typing_on) keys[n] = ROW_TYPING, hashes[n] = 0, lines[n++] = NULL;
    else if (waiting) keys[n] = ROW_WAITING, hashes[n] = dk_hash_str(DK_HASH0, a->name), lines[n++] = NULL;

    uint32_t sig = dk_hash_str(DK_HASH0, c.agent_id);
    sig = dk_hash(sig, keys, sizeof(keys[0]) * n);
    sig = dk_hash(sig, hashes, sizeof(hashes[0]) * n);
    if (sig == s_chat_sig && s_rows_n == n) return;
    s_chat_sig = sig;

    bool new_line = tail && strcmp(tail->id, c.last_line) != 0;
    // Follow the newest line if you're at the bottom (or just opened the chat); if you've
    // scrolled back to read, new lines arrive below without moving you.
    bool opening = c.last_line[0] == '\0';
    bool follow = !c.keep_place && (c.follow || near_bottom() || opening);
    c.keep_place = false;
    lv_coord_t keep = lv_obj_get_scroll_y(c.bubbles);

    // Old lines that scrolled out of the relay's window: drop them from the top.
    int first = 0;
    if (n && s_rows_n) {
        int at = -1;
        for (int i = 0; i < s_rows_n; i++)
            if (s_rows[i].key == keys[0]) {
                at = i;
                break;
            }
        if (at > 0) {
            lv_coord_t gone = 0;
            for (int i = 0; i < at; i++) gone += lv_obj_get_height(s_rows[i].obj) + 12;
            for (int i = 0; i < at; i++) lv_obj_delete(s_rows[i].obj);
            memmove(&s_rows[0], &s_rows[at], sizeof(s_rows[0]) * (s_rows_n - at));
            s_rows_n -= at;
            keep -= gone; // the lines you're reading stay put
            if (keep < 0) keep = 0;
        } else if (at < 0) {
            rows_clear();
        }
    }
    // Keep the rows that still match; rebuild from the first that doesn't.
    while (first < s_rows_n && first < n && s_rows[first].key == keys[first] && s_rows[first].hash == hashes[first]) first++;
    rows_drop_from(first);
    if (!a) return;

    lv_obj_t *last = NULL;
    for (int i = first; i < n; i++) {
        lv_obj_t *o;
        switch (keys[i]) {
        case ROW_EMPTY_PROMPT: o = bubble(a->last_prompt, true, "From your computer", NULL, NULL, NULL); break;
        case ROW_EMPTY_REPLY: o = bubble(a->last_message, false, dk_source_label(a->source), NULL, NULL, NULL); break;
        case ROW_EMPTY:
            o = dk_label(c.bubbles, DK_FONT_M, DK_MUTED, "Nothing said yet. Send a message or a quick reply.");
            lv_obj_set_width(o, lv_pct(100));
            lv_label_set_long_mode(o, LV_LABEL_LONG_MODE_WRAP);
            break;
        case ROW_TYPING: o = typing(); break;
        case ROW_WAITING: o = waiting_card(); break;
        default: o = line_row(a, lines[i]); break;
        }
        s_rows[s_rows_n].key = keys[i];
        s_rows[s_rows_n].hash = hashes[i];
        s_rows[s_rows_n++].obj = o;
        if (lines[i]) last = o;
    }
    if (animate_new && last && new_line) dk_anim_pop(lv_obj_get_child(last, 0), 0);
    snprintf(c.last_line, sizeof(c.last_line), "%s", tail ? tail->id : "");
    if (first == n) return; // nothing new to lay out
    lv_obj_update_layout(c.bubbles);
    if (follow) {
        lv_obj_scroll_to_y(c.bubbles, LV_COORD_MAX, animate_new ? LV_ANIM_ON : LV_ANIM_OFF);
        c.follow = true;
    } else {
        lv_obj_scroll_to_y(c.bubbles, keep, LV_ANIM_OFF);
        c.follow = near_bottom();
    }
}

static void rebuild(bool animate_new)
{
    const dk_agent_t *a = dk_model_find_agent(g_model, c.agent_id);
    rebuild_head(a);
    rebuild_chat(a, animate_new);
}

const char *scr_chat_agent(void)
{
    return c.agent_id;
}

void scr_chat_show(const char *agent_id)
{
    if (strcmp(c.agent_id, agent_id ? agent_id : "") != 0) {
        open_clear();
        rows_clear();
    }
    snprintf(c.agent_id, sizeof(c.agent_id), "%s", agent_id ? agent_id : "");
    reader_close();
    c.last_line[0] = '\0';
    c.follow = true;
    lv_textarea_set_text(c.ta, "");
    kb_hide();
    s_head_sig = s_chat_sig = 0; // always build afresh when opening a chat
    rebuild(false);
}

void scr_chat_images_changed(void)
{
    if (!c.scr || !c.agent_id[0]) return;
    // A picture arrived: redraw in place, without moving you if you'd scrolled back.
    bool at_bottom = c.follow;
    lv_coord_t y = lv_obj_get_scroll_y(c.bubbles);
    s_chat_sig = 0;
    rebuild(false);
    if (!at_bottom) lv_obj_scroll_to_y(c.bubbles, y, LV_ANIM_OFF);
}

void scr_chat_refresh(dk_cause_t cause)
{
    if (!c.scr || !c.agent_id[0]) return;
    rebuild(cause == DK_CAUSE_CHAT);
}
