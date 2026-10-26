// One session up close: what it's doing, its recent messages, quick replies and a keyboard.
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

static struct {
    lv_obj_t *scr, *face, *badge_row, *name, *status, *delivery;
    lv_obj_t *right, *bubbles, *quick, *input_row, *ta, *kb, *helpers, *helper_list, *listen, *listen_label, *usage;
    bool listening;
    bool can_message;
    bool follow; // the conversation is pinned to its newest line (you haven't scrolled back)
    char agent_id[72];
    char last_line[16];
} c;

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
    lv_label_set_long_mode(c.name, LV_LABEL_LONG_MODE_DOTS);
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
    // "Keep listening": when it finishes, it waits for your next message from here.
    c.listen = dk_button(info, "Keep listening", false, DK_FONT_S);
    lv_obj_set_width(c.listen, lv_pct(100));
    lv_obj_set_style_min_height(c.listen, 48, 0);
    lv_obj_set_style_margin_top(c.listen, 8, 0);
    c.listen_label = lv_obj_get_child(c.listen, 0);
    lv_obj_add_event_cb(c.listen, listen_cb, LV_EVENT_CLICKED, NULL);

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

static lv_obj_t *bubble(const char *text, bool mine, const char *meta, const char *image, const char *quote)
{
    lv_obj_t *wrap = dk_box(c.bubbles);
    lv_obj_set_width(wrap, lv_pct(100));
    dk_col(wrap, 4);
    lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_START, mine ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, mine ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START);
    lv_obj_t *b = dk_card(wrap, mine ? DK_INK : DK_LINE, 24);
    lv_obj_set_style_pad_hor(b, 18, 0);
    lv_obj_set_style_pad_ver(b, 14, 0);
    if ((image && image[0]) || (quote && quote[0])) dk_col(b, 10);
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
    if (text && text[0]) {
        lv_obj_t *l = dk_label(b, DK_FONT_M, mine ? DK_GROUND : DK_INK, text);
        lv_obj_set_style_max_width(l, 680, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
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
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    return row;
}

static void typing(void)
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
}

static uint32_t s_head_sig, s_chat_sig;

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
    dk_set_text(c.delivery, a->channel ? "Live channel: messages arrive straight away."
                            : a->listening ? "Listening: when Claude finishes, it waits for your next message from here."
                            : a->can_message ? "Messages arrive when Claude finishes its current step, or with your next prompt."
                                             : "This tool can't take messages from Deskling yet.");
    if (a->can_message && !a->channel) {
        lv_obj_remove_flag(c.listen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(c.listen, a->listening ? DK_INK : DK_CARD, 0);
        lv_obj_set_style_border_width(c.listen, a->listening ? 0 : 3, 0);
        lv_obj_set_style_text_color(c.listen_label, a->listening ? DK_GROUND : DK_INK, 0);
        dk_set_text(c.listen_label, a->listening ? "Listening (tap to stop)" : "Keep listening");
    } else {
        lv_obj_add_flag(c.listen, LV_OBJ_FLAG_HIDDEN);
    }
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
            lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_t *t = dk_label(col, DK_FONT_XS, DK_MUTED, h->done ? "Finished" : (h->task[0] ? h->task : "Working"));
            lv_obj_set_width(t, lv_pct(100));
            lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
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

static void rebuild_chat(const dk_agent_t *a, bool animate_new)
{
    const dk_chat_t *tail = a && a->chat_n ? &a->chat[a->chat_n - 1] : NULL;
    // "Typing" only while it's actually busy: not on a session that's gone quiet.
    bool recent = !g_model->now || !a->updated || g_model->now - a->updated < 3 * 60 * 1000;
    bool typing_on = tail && recent && (tail->step || (tail->from_you && tail->status != DK_CHAT_QUEUED)) && a->state == DK_WORKING;
    uint32_t sig = dk_hash_str(DK_HASH0, c.agent_id);
    if (a) {
        for (int i = 0; i < a->chat_n; i++) {
            sig = dk_hash_str(sig, a->chat[i].id);
            sig = dk_hash_str(sig, a->chat[i].image);
            sig = dk_hash(sig, &a->chat[i].status, sizeof(a->chat[i].status));
        }
        if (!a->chat_n) {
            sig = dk_hash_str(sig, a->last_prompt);
            sig = dk_hash_str(sig, a->last_message);
        }
        sig = dk_hash(sig, &typing_on, sizeof(typing_on));
    }
    if (sig == s_chat_sig) return;
    s_chat_sig = sig;

    bool new_line = tail && strcmp(tail->id, c.last_line) != 0;
    // Follow the newest line if you're at the bottom (or just opened the chat); if you've
    // scrolled back to read, new lines arrive below without moving you.
    bool opening = c.last_line[0] == '\0';
    bool follow = c.follow || near_bottom() || opening;
    lv_coord_t keep = lv_obj_get_scroll_y(c.bubbles);
    lv_obj_clean(c.bubbles);
    lv_obj_t *spacer = dk_box(c.bubbles);
    lv_obj_set_size(spacer, 1, 0);
    lv_obj_set_flex_grow(spacer, 1);
    if (!a) return;

    if (a->chat_n == 0) {
        if (a->last_prompt[0]) bubble(a->last_prompt, true, "From your computer", NULL, NULL);
        if (a->last_message[0]) bubble(a->last_message, false, dk_source_label(a->source), NULL, NULL);
        if (!a->last_prompt[0] && !a->last_message[0]) {
            lv_obj_t *l = dk_label(c.bubbles, DK_FONT_M, DK_MUTED, "Nothing said yet. Send a message or a quick reply.");
            lv_obj_set_width(l, lv_pct(100));
            lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        }
    }
    lv_obj_t *last = NULL;
    for (int i = 0; i < a->chat_n; i++) {
        const dk_chat_t *line = &a->chat[i];
        last = line->step ? step_row(line->text, line->thought) : bubble(line->text, line->from_you, chat_meta(a, line), line->image, line->quote);
    }
    if (typing_on) typing();
    if (animate_new && last && new_line) dk_anim_pop(lv_obj_get_child(last, 0), 0);
    snprintf(c.last_line, sizeof(c.last_line), "%s", tail ? tail->id : "");
    lv_obj_update_layout(c.bubbles);
    if (follow) {
        lv_obj_scroll_to_y(c.bubbles, LV_COORD_MAX, animate_new ? LV_ANIM_ON : LV_ANIM_OFF);
        c.follow = true;
    } else {
        lv_obj_scroll_to_y(c.bubbles, keep, LV_ANIM_OFF);
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
    snprintf(c.agent_id, sizeof(c.agent_id), "%s", agent_id ? agent_id : "");
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
