// Full-screen takeover when an agent needs you: a permission (Allow / Deny) or a question (tap an answer).
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define MAX_DONE 8

static struct {
    lv_obj_t *scr, *ring[2], *face, *timer_pill, *timer_label, *body;
    dk_pending_t cur; // a copy: the model can change underneath us
    bool have;
    bool result; // showing "Sent!" after an answer
    char answered[MAX_DONE][16];
    int answered_n;
    int q_index;
    uint8_t picks[DK_MAX_QUESTIONS]; // bitmask of chosen options per question
    lv_timer_t *back_timer;
} a;



static bool was_answered(const char *id)
{
    for (int i = 0; i < a.answered_n; i++) {
        if (strcmp(a.answered[i], id) == 0) return true;
    }
    return false;
}

static void remember(const char *id)
{
    if (a.answered_n == MAX_DONE) {
        memmove(a.answered[0], a.answered[1], sizeof(a.answered[0]) * (MAX_DONE - 1));
        a.answered_n--;
    }
    snprintf(a.answered[a.answered_n++], sizeof(a.answered[0]), "%s", id);
}

static void back_cb(lv_timer_t *t)
{
    (void)t;
    a.back_timer = NULL;
    a.result = false;
    a.have = false;
    ui_alert_finished();
}

static void show_result(const char *title, const char *sub, lv_color_t bg);

// ------------------------------------------------------------ actions

static const char *who(void);

static void decide_cb(lv_event_t *e)
{
    const char *d = lv_event_get_user_data(e);
    dk_send_decision(a.cur.id, d);
    remember(a.cur.id);
    ui_sound(strcmp(d, "allow") == 0 ? DK_SND_ALLOW : strcmp(d, "deny") == 0 ? DK_SND_DENY : DK_SND_MESSAGE);
    static char line[96];
    if (strcmp(d, "allow") == 0) {
        snprintf(line, sizeof(line), "%s is carrying on.", who());
        show_result("Sent! Back to work.", line, DK_GOOD_BG);
    } else if (strcmp(d, "deny") == 0) {
        snprintf(line, sizeof(line), "Okay, told %s no.", who());
        show_result(line, "It'll stop and wait for you.", DK_LILAC_SOFT);
    }
    else show_result("Over to your computer.", "The prompt is waiting there for you.", DK_BUTTER_BG);
}

static void build_question(void);

static void submit_answers(void)
{
    static char joined[DK_MAX_QUESTIONS][512];
    const char *labels[DK_MAX_QUESTIONS] = {0};
    for (int q = 0; q < a.cur.question_n; q++) {
        joined[q][0] = '\0';
        for (int o = 0; o < a.cur.questions[q].option_n; o++) {
            if (!(a.picks[q] & (1u << o))) continue;
            if (joined[q][0]) strncat(joined[q], "\n", sizeof(joined[q]) - strlen(joined[q]) - 1);
            strncat(joined[q], a.cur.questions[q].options[o].label, sizeof(joined[q]) - strlen(joined[q]) - 1);
        }
        labels[q] = joined[q];
    }
    dk_send_answer(&a.cur, labels);
    remember(a.cur.id);
    ui_sound(DK_SND_ALLOW);
    char title[96];
    if (a.cur.question_n == 1 && !a.cur.questions[0].multi) snprintf(title, sizeof(title), "Going with %.80s!", joined[0]);
    else snprintf(title, sizeof(title), "Answers sent!");
    static char line[96];
    snprintf(line, sizeof(line), "%s is carrying on with your answer.", who());
    show_result(title, line, DK_GOOD_BG);
}

static void option_cb(lv_event_t *e)
{
    int o = (int)(intptr_t)lv_event_get_user_data(e);
    dk_question_t *q = &a.cur.questions[a.q_index];
    if (q->multi) {
        a.picks[a.q_index] ^= (uint8_t)(1u << o);
        lv_obj_t *btn = lv_event_get_current_target(e);
        bool on = a.picks[a.q_index] & (1u << o);
        lv_obj_set_style_bg_color(btn, on ? DK_INK : DK_CARD, 0);
        for (uint32_t i = 0; i < lv_obj_get_child_count(btn); i++) {
            lv_obj_set_style_text_color(lv_obj_get_child(btn, i), on ? DK_GROUND : (i == 0 ? DK_INK : DK_INK_SOFT), 0);
        }
        return;
    }
    a.picks[a.q_index] = (uint8_t)(1u << o);
    if (a.q_index + 1 < a.cur.question_n) {
        a.q_index++;
        build_question();
    } else {
        submit_answers();
    }
}

static void next_cb(lv_event_t *e)
{
    (void)e;
    if (a.q_index + 1 < a.cur.question_n) {
        a.q_index++;
        build_question();
    } else {
        submit_answers();
    }
}

static void computer_cb(lv_event_t *e)
{
    (void)e;
    dk_send_decision(a.cur.id, "computer");
    remember(a.cur.id);
    show_result("Over to your computer.", "The question is waiting there for you.", DK_BUTTER_BG);
}

// ------------------------------------------------------------- building

static lv_obj_t *fresh_body(void)
{
    lv_obj_clean(a.body);
    return a.body;
}

static const char *who(void)
{
    return a.cur.source == DK_SRC_CLAUDE ? "Claude" : a.cur.source == DK_SRC_CUSTOM ? a.cur.name : dk_source_label(a.cur.source);
}

static void build_permission(void)
{
    lv_obj_t *b = fresh_body();
    lv_obj_set_style_bg_color(a.scr, DK_ALERT_BG, 0);
    deskling_set_mood(a.face, DK_MOOD_WORRIED);
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_border_color(a.ring[i], DK_ALERT_RING, 0);
        lv_obj_remove_flag(a.ring[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *meta = dk_box(b);
    dk_row(meta, 12);
    dk_pill(meta, lv_color_hex(0xFAC3B3), DK_FONT_L, a.cur.name);
    char line[120];
    snprintf(line, sizeof(line), "%s · %s", a.cur.host, dk_source_label(a.cur.source));
    dk_label(meta, DK_FONT_L, lv_color_hex(0x4A3F55), line);

    const char *t = a.cur.tool;
    const char *what = "wants to use ";
    char title[160];
    if (!strcmp(t, "Bash") || !strcmp(t, "PowerShell") || !strcmp(t, "Shell")) snprintf(title, sizeof(title), "%s wants to run a command", who());
    else if (!strcmp(t, "Edit") || !strcmp(t, "MultiEdit") || !strcmp(t, "NotebookEdit")) snprintf(title, sizeof(title), "%s wants to edit a file", who());
    else if (!strcmp(t, "Write")) snprintf(title, sizeof(title), "%s wants to write a file", who());
    else if (!strcmp(t, "WebFetch")) snprintf(title, sizeof(title), "%s wants to open a web page", who());
    else if (!strcmp(t, "WebSearch")) snprintf(title, sizeof(title), "%s wants to search the web", who());
    else snprintf(title, sizeof(title), "%s %s%s", who(), what, t);
    lv_obj_t *h = dk_label(b, strlen(title) > 34 ? DK_FONT_H2 : DK_FONT_H1, DK_INK, title);
    lv_obj_set_width(h, lv_pct(100));
    lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_WRAP);

    lv_obj_t *code = dk_card(b, DK_CARD, 24);
    lv_obj_set_width(code, lv_pct(100));
    lv_obj_set_style_border_width(code, 3, 0);
    lv_obj_set_style_border_color(code, DK_INK, 0);
    lv_obj_set_style_pad_hor(code, 26, 0);
    lv_obj_set_style_pad_ver(code, 20, 0);
    lv_obj_t *cl = dk_label(code, DK_FONT_MONO, DK_INK, a.cur.summary[0] ? a.cur.summary : "(no details)");
    lv_obj_set_width(cl, lv_pct(100));
    lv_label_set_long_mode(cl, LV_LABEL_LONG_MODE_WRAP);

    if (a.cur.detail[0]) {
        lv_obj_t *d = dk_label(b, DK_FONT_L, lv_color_hex(0x4A3F55), a.cur.detail);
        lv_obj_set_width(d, lv_pct(100));
        lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_WRAP);
    }

    lv_obj_t *row = dk_box(b);
    lv_obj_set_width(row, lv_pct(100));
    dk_row(row, 16);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_t *allow = dk_button(row, "Allow once", true, DK_FONT_XL);
    lv_obj_set_height(allow, 84);
    lv_obj_set_flex_grow(allow, 13);
    lv_obj_set_style_radius(allow, 28, 0);
    dk_anim_glow(allow, DK_INK);
    lv_obj_add_event_cb(allow, decide_cb, LV_EVENT_CLICKED, "allow");
    lv_obj_t *deny = dk_button(row, "Deny", false, DK_FONT_XL);
    lv_obj_set_height(deny, 84);
    lv_obj_set_flex_grow(deny, 10);
    lv_obj_set_style_radius(deny, 28, 0);
    lv_obj_add_event_cb(deny, decide_cb, LV_EVENT_CLICKED, "deny");
    lv_obj_t *later = dk_button(row, "On my computer", false, DK_FONT_L);
    lv_obj_set_height(later, 84);
    lv_obj_set_flex_grow(later, 10);
    lv_obj_set_style_radius(later, 28, 0);
    lv_obj_set_style_bg_opa(later, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(later, decide_cb, LV_EVENT_CLICKED, "computer");

    dk_anim_rise(b, 0);
}

static void build_question(void)
{
    lv_obj_t *b = fresh_body();
    lv_obj_set_style_bg_color(a.scr, DK_BUTTER_BG, 0);
    deskling_set_mood(a.face, DK_MOOD_CURIOUS);
    for (int i = 0; i < 2; i++) lv_obj_add_flag(a.ring[i], LV_OBJ_FLAG_HIDDEN);

    const dk_question_t *q = &a.cur.questions[a.q_index];
    char head[96];
    if (a.cur.question_n > 1) snprintf(head, sizeof(head), "%s HAS A QUESTION · %d OF %d", a.cur.source == DK_SRC_CLAUDE ? "CLAUDE" : "IT", a.q_index + 1, a.cur.question_n);
    else snprintf(head, sizeof(head), "%s HAS A QUESTION%s%s", a.cur.source == DK_SRC_CLAUDE ? "CLAUDE" : "IT", q->header[0] ? " · " : "", q->header);
    dk_label(b, DK_FONT_S, DK_BUTTER_INK, head);

    lv_obj_t *t = dk_label(b, strlen(q->question) > 60 ? DK_FONT_H2 : DK_FONT_H1, DK_INK, q->question);
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);

    lv_obj_t *grid = dk_box(b);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 14, 0);
    lv_obj_set_style_pad_row(grid, 14, 0);
    lv_obj_set_style_pad_top(grid, 6, 0);
    for (int o = 0; o < q->option_n; o++) {
        bool on = a.picks[a.q_index] & (1u << o);
        lv_obj_t *btn = lv_button_create(grid);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, lv_pct(48), LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(btn, 100, 0);
        lv_obj_set_style_radius(btn, 26, 0);
        lv_obj_set_style_border_width(btn, 3, 0);
        lv_obj_set_style_border_color(btn, DK_INK, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, on ? DK_INK : DK_CARD, 0);
        lv_obj_set_style_pad_hor(btn, 20, 0);
        lv_obj_set_style_pad_ver(btn, 16, 0);
        dk_col(btn, 4);
        lv_obj_t *l = dk_label(btn, DK_FONT_XL, on ? DK_GROUND : DK_INK, q->options[o].label);
        lv_obj_set_width(l, lv_pct(100));
        dk_one_line(l);
        if (q->options[o].description[0]) {
            lv_obj_t *d = dk_label(btn, DK_FONT_S, on ? DK_GROUND : DK_INK_SOFT, q->options[o].description);
            lv_obj_set_width(d, lv_pct(100));
            lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_WRAP);
        }
        lv_obj_add_event_cb(btn, option_cb, LV_EVENT_CLICKED, (void *)(intptr_t)o);
        dk_press_style(btn);
        dk_anim_rise(btn, 80 + o * 50);
    }

    lv_obj_t *row = dk_box(b);
    dk_row(row, 16);
    lv_obj_set_style_pad_top(row, 4, 0);
    if (q->multi || a.cur.question_n > 1) {
        bool last = a.q_index + 1 >= a.cur.question_n;
        lv_obj_t *next = dk_button(row, q->multi && last ? "Send answers" : (last ? "Send" : "Next"), true, DK_FONT_L);
        lv_obj_add_event_cb(next, next_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *pc = dk_button(row, "Answer on my computer instead", false, DK_FONT_M);
    lv_obj_set_style_bg_opa(pc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pc, 0, 0);
    lv_obj_set_style_text_decor(lv_obj_get_child(pc, 0), LV_TEXT_DECOR_UNDERLINE, 0);
    lv_obj_add_event_cb(pc, computer_cb, LV_EVENT_CLICKED, NULL);

    dk_anim_rise(t, 0);
}

static void show_result(const char *title, const char *sub, lv_color_t bg)
{
    a.result = true;
    lv_obj_t *b = fresh_body();
    lv_obj_set_style_bg_color(a.scr, bg, 0);
    for (int i = 0; i < 2; i++) lv_obj_add_flag(a.ring[i], LV_OBJ_FLAG_HIDDEN);
    deskling_set_mood(a.face, DK_MOOD_HAPPY);
    lv_obj_add_flag(a.timer_pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *t = dk_label(b, DK_FONT_H1, DK_INK, title);
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
    dk_label(b, DK_FONT_XL, DK_INK_SOFT, sub);
    dk_anim_rise(b, 0);
    if (a.back_timer) lv_timer_delete(a.back_timer);
    a.back_timer = lv_timer_create(back_cb, 1800, NULL);
    lv_timer_set_repeat_count(a.back_timer, 1);
}

lv_obj_t *scr_alert_create(void)
{
    a.scr = lv_obj_create(NULL);
    lv_obj_remove_flag(a.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(a.scr, DK_ALERT_BG, 0);
    lv_obj_set_style_bg_opa(a.scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(a.scr, 56, 0);
    lv_obj_set_style_pad_ver(a.scr, 40, 0);
    dk_row(a.scr, 48);

    lv_obj_t *left = dk_box(a.scr);
    lv_obj_set_width(left, 340);
    dk_col(left, 20);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *stage = dk_box(left);
    lv_obj_set_size(stage, 320, 320);
    dk_overflow(stage, 260);
    dk_overflow(left, 260);
    for (int i = 0; i < 2; i++) {
        a.ring[i] = dk_box(stage);
        lv_obj_set_size(a.ring[i], 300, 300);
        lv_obj_center(a.ring[i]);
        lv_obj_set_style_radius(a.ring[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(a.ring[i], 6, 0);
        lv_obj_set_style_border_color(a.ring[i], DK_ALERT_RING, 0);
        dk_anim_ping(a.ring[i], 2000, i * 1000);
    }
    a.face = deskling_create(stage, 280, DK_FACE);
    lv_obj_center(a.face);

    a.timer_pill = dk_card(left, DK_CARD, LV_RADIUS_CIRCLE);
    lv_obj_set_style_pad_hor(a.timer_pill, 20, 0);
    lv_obj_set_style_pad_ver(a.timer_pill, 10, 0);
    dk_row(a.timer_pill, 10);
    dk_dot(a.timer_pill, 12, DK_ALERT_INK, true);
    a.timer_label = dk_label(a.timer_pill, DK_FONT_L, DK_INK, "Waiting");

    a.body = dk_box(a.scr);
    lv_obj_set_flex_grow(a.body, 1);
    lv_obj_set_width(a.body, 0);
    dk_col(a.body, 20);
    return a.scr;
}

bool scr_alert_refresh(void)
{
    if (!a.scr) return false;
    const dk_pending_t *next = NULL;
    for (int i = 0; i < g_model->pending_n; i++) {
        if (!was_answered(g_model->pending[i].id)) {
            next = &g_model->pending[i];
            break;
        }
    }
    if (a.result) return true; // let the "Sent!" moment finish
    if (!next) {
        a.have = false;
        return false;
    }
    if (a.have && strcmp(a.cur.id, next->id) == 0) return true;
    memcpy(&a.cur, next, sizeof(a.cur));
    a.have = true;
    a.q_index = 0;
    memset(a.picks, 0, sizeof(a.picks));
    lv_obj_remove_flag(a.timer_pill, LV_OBJ_FLAG_HIDDEN);
    if (a.cur.is_question && a.cur.question_n > 0) build_question();
    else build_permission();
    scr_alert_tick();
    return true;
}

void scr_alert_tick(void)
{
    if (!a.have || a.result) return;
    int64_t left = (a.cur.expires - (ui_now_ms() + ui_relay_offset_ms())) / 1000;
    if (left < 0) left = 0;
    lv_label_set_text_fmt(a.timer_label, "%s %d:%02d", left > 0 ? "Waiting" : "Over to your computer", (int)(left / 60), (int)(left % 60));
}
