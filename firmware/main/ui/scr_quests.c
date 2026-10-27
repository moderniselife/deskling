// Quests: today's tickets from Mojo Desk, Linear and Jira as a game. Deskling's level, XP,
// streak and combo on the left with the week's challenges; today's quests on the right (bosses
// have HP), the rest below to add; a focus timer that takes the whole screen.
//
// Drawing stays cheap: the left panel only changes label text and bar values; the quest list
// is rebuilt only when what it shows changed (a hash), and the focus timer redraws its ring and
// one label once a second.
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define ARM_MS 3000 // the check button waits this long for its second tap

static struct {
    lv_obj_t *scr, *face, *ring, *level, *xp_text, *xp_bar, *streak, *combo, *combo_bar, *today, *week;
    lv_obj_t *title_sub, *segs, *sources, *error, *error_text, *list, *empty;
    lv_obj_t *focus, *focus_ring, *focus_time, *focus_key, *focus_title, *focus_hp, *focus_hp_text, *focus_face;
    lv_timer_t *tick;
    char open_id[56];  // the quest showing its actions
    char armed_id[56]; // its check button was tapped once
    uint32_t armed_at;
    uint32_t list_sig, week_sig;
    int32_t shown_xp; // what the XP bar shows, to animate from
} q;

static const char *SRC_INITIAL[] = {"M", "L", "J"};

static lv_color_t src_color(dk_qsrc_t s)
{
    return s == DK_QSRC_LINEAR ? DK_INK : s == DK_QSRC_JIRA ? DK_SWEAT : DK_PURPLE;
}

static const char *status_label(dk_qstatus_t s)
{
    return s == DK_Q_PROGRESS ? "In progress" : s == DK_Q_REVIEW ? "Ready to test" : s == DK_Q_DONE ? "Done" : "To do";
}

static void est_text(int min, char *buf, size_t n)
{
    if (min <= 0) buf[0] = '\0';
    else if (min < 60) snprintf(buf, n, "%dm", min);
    else if (min % 60) snprintf(buf, n, "%dh %dm", min / 60, min % 60);
    else snprintf(buf, n, "%dh", min / 60);
}

/* Each button that acts on a quest keeps its own copy of the id (snapshots rewrite the model). */
static void id_free(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static void on_click(lv_obj_t *o, lv_event_cb_t cb, const char *id)
{
    size_t n = strlen(id) + 1;
    char *c = lv_malloc(n);
    if (!c) return;
    memcpy(c, id, n);
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, c);
    lv_obj_add_event_cb(o, id_free, LV_EVENT_DELETE, c);
}

static lv_obj_t *small_button(lv_obj_t *p, const char *text, bool filled)
{
    lv_obj_t *b = dk_button(p, text, filled, DK_FONT_S);
    lv_obj_set_style_min_height(b, 48, 0);
    lv_obj_set_style_pad_hor(b, 18, 0);
    lv_obj_set_style_pad_ver(b, 8, 0);
    return b;
}

static void refresh_list(void);
static void refresh_focus(void);

// ------------------------------------------------------------ actions

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_go(SCR_HOME);
}

static void sync_cb(lv_event_t *e)
{
    (void)e;
    dk_send_quest_sync();
    dk_anim_shake(lv_event_get_target_obj(e));
}

static void open_cb(lv_event_t *e)
{
    const char *id = lv_event_get_user_data(e);
    snprintf(q.open_id, sizeof(q.open_id), "%s", strcmp(q.open_id, id) == 0 ? "" : id);
    refresh_list();
}

static void check_cb(lv_event_t *e)
{
    const char *id = lv_event_get_user_data(e);
    uint32_t now = lv_tick_get();
    if (strcmp(q.armed_id, id) == 0 && lv_tick_diff(now, q.armed_at) < ARM_MS) {
        // Second tap: done. The relay moves it in its service and the score comes back.
        q.armed_id[0] = '\0';
        dk_send_quest_status(id, DK_Q_DONE);
        deskling_celebrate(q.face, 1400);
        lv_obj_t *b = lv_event_get_target_obj(e);
        lv_obj_set_style_bg_color(b, DK_GOOD_INK, 0);
        dk_anim_pop(b, 0);
        return;
    }
    snprintf(q.armed_id, sizeof(q.armed_id), "%s", id);
    q.armed_at = now;
    refresh_list();
}

static void status_cb(lv_event_t *e, dk_qstatus_t to)
{
    dk_send_quest_status(lv_event_get_user_data(e), to);
    if (to == DK_Q_DONE) deskling_celebrate(q.face, 1400);
}
static void start_cb(lv_event_t *e) { status_cb(e, DK_Q_PROGRESS); }
static void review_cb(lv_event_t *e) { status_cb(e, DK_Q_REVIEW); }
static void done_cb(lv_event_t *e) { status_cb(e, DK_Q_DONE); }
static void todo_cb(lv_event_t *e) { status_cb(e, DK_Q_TODO); }

static void focus_cb(lv_event_t *e)
{
    dk_send_quest_focus(lv_event_get_user_data(e), 25);
}
static void add_cb(lv_event_t *e)
{
    dk_send_quest_plan(lv_event_get_user_data(e), true);
    dk_anim_pop(lv_event_get_target_obj(e), 0);
}
static void remove_cb(lv_event_t *e) { dk_send_quest_plan(lv_event_get_user_data(e), false); }
static void snooze_cb(lv_event_t *e) { dk_send_quest_snooze(lv_event_get_user_data(e)); }
static void up_cb(lv_event_t *e) { dk_send_quest_move(lv_event_get_user_data(e), -1); }
static void down_cb(lv_event_t *e) { dk_send_quest_move(lv_event_get_user_data(e), 1); }

static void claude_cb(lv_event_t *e)
{
    const dk_quest_t *t = dk_quest_find(g_model, lv_event_get_user_data(e));
    if (!t) return;
    char prompt[420];
    snprintf(prompt, sizeof(prompt), "Work on %s (%s): %s. Read the ticket first: %s", t->key, dk_qsrc_label(t->src), t->title, t->url);
    scr_new_prefill(prompt);
    ui_go(SCR_NEW);
}

static void focus_stop_cb(lv_event_t *e)
{
    (void)e;
    dk_send_quest_focus(NULL, 0);
}

static void focus_done_cb(lv_event_t *e)
{
    (void)e;
    const char *id = g_model->quests.focus_id;
    if (id[0]) dk_send_quest_status(id, DK_Q_DONE); // finishing it ends the block too
    deskling_celebrate(q.focus_face, 1600);
}

// ------------------------------------------------------------ building

static lv_obj_t *badge(lv_obj_t *p, dk_qsrc_t s, int32_t size)
{
    lv_obj_t *b = dk_card(p, src_color(s), size / 3);
    lv_obj_set_size(b, size, size);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_t *l = dk_label(b, size >= 40 ? DK_FONT_L : DK_FONT_S, DK_GROUND, SRC_INITIAL[s]);
    lv_obj_center(l);
    return b;
}

static lv_obj_t *bar(lv_obj_t *p, int32_t w, int32_t h, lv_color_t bg, lv_color_t fg)
{
    lv_obj_t *b = lv_bar_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(b, fg, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(b, 700, 0);
    return b;
}

static void quest_card(lv_obj_t *list, const dk_quest_t *t, int index)
{
    bool done = t->status == DK_Q_DONE;
    bool open = strcmp(q.open_id, t->id) == 0;
    lv_obj_t *c = dk_card(list, t->boss && !done ? DK_ALERT_BG : DK_CARD, 26);
    lv_obj_set_width(c, lv_pct(100));
    lv_obj_set_style_pad_hor(c, 18, 0);
    lv_obj_set_style_pad_ver(c, 14, 0);
    dk_col(c, 10);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    on_click(c, open_cb, t->id);
    if (done) lv_obj_set_style_opa(c, LV_OPA_60, 0);

    lv_obj_t *row = dk_box(c);
    lv_obj_set_width(row, lv_pct(100));
    dk_row(row, 14);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    badge(row, t->src, 44);
    lv_obj_t *col = dk_box(row);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_width(col, 0);
    dk_col(col, 2);
    char meta[96], est[16];
    est_text(t->est, est, sizeof(est));
    snprintf(meta, sizeof(meta), "%s  ·  %s%s%s%s%s", t->key, status_label(t->status), est[0] ? "  ·  " : "", est, t->due[0] ? "  ·  due " : "", t->due[0] ? t->due + 5 : "");
    lv_obj_t *m = dk_label(col, DK_FONT_XS, t->overdue ? DK_ALERT_INK : DK_MUTED, meta);
    lv_obj_set_width(m, lv_pct(100));
    dk_one_line(m);
    lv_obj_t *ti = dk_label(col, DK_FONT_L, DK_INK, t->title);
    lv_obj_set_width(ti, lv_pct(100));
    dk_one_line(ti);
    if (done) lv_obj_set_style_text_decor(ti, LV_TEXT_DECOR_STRIKETHROUGH, 0);

    if (t->boss && !done) {
        lv_obj_t *tag = dk_pill(row, DK_ALERT_PILL, DK_FONT_XS, "Boss");
        lv_obj_set_style_text_color(tag, DK_ALERT_INK, 0);
        dk_anim_wiggle(tag);
    }
    // The big round check: tap, then tap again within 3 s.
    bool armed = strcmp(q.armed_id, t->id) == 0 && lv_tick_diff(lv_tick_get(), q.armed_at) < ARM_MS;
    lv_obj_t *ck = lv_button_create(row);
    lv_obj_remove_style_all(ck);
    lv_obj_set_size(ck, armed ? 132 : 56, 56);
    lv_obj_set_style_radius(ck, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ck, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ck, done ? DK_GOOD_INK : armed ? DK_BUTTER : DK_GROUND, 0);
    lv_obj_set_style_border_width(ck, done || armed ? 0 : 3, 0);
    lv_obj_set_style_border_color(ck, DK_INK, 0);
    lv_obj_t *cl = dk_label(ck, armed ? DK_FONT_S : &lv_font_montserrat_28, done ? DK_GROUND : DK_INK, armed ? "Tap: done" : LV_SYMBOL_OK);
    lv_obj_center(cl);
    if (!done) {
        on_click(ck, check_cb, t->id);
        dk_press_style(ck);
        if (armed) dk_anim_pop(ck, 0);
    }

    if (t->boss && !done && t->hp_max) {
        lv_obj_t *hr = dk_box(c);
        lv_obj_set_width(hr, lv_pct(100));
        dk_row(hr, 12);
        lv_obj_set_flex_align(hr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *hb = bar(hr, 10, 14, DK_CARD, DK_ALERT_RING);
        lv_obj_set_flex_grow(hb, 1);
        lv_bar_set_range(hb, 0, t->hp_max);
        lv_bar_set_value(hb, t->hp, LV_ANIM_OFF);
        char hp[32];
        snprintf(hp, sizeof(hp), "HP %d / %d", t->hp, t->hp_max);
        dk_label(hr, DK_FONT_XS, DK_ALERT_INK, hp);
    }

    if (!open) return;
    // Its actions, in the order you'd want them.
    lv_obj_t *acts = dk_box(c);
    lv_obj_set_width(acts, lv_pct(100));
    dk_row(acts, 10);
    lv_obj_set_flex_flow(acts, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(acts, 10, 0);
    if (t->status == DK_Q_TODO) on_click(small_button(acts, "Start", true), start_cb, t->id);
    if (t->status == DK_Q_PROGRESS) on_click(small_button(acts, "Ready to test", false), review_cb, t->id);
    if (!done) {
        on_click(small_button(acts, "Done", t->status != DK_Q_TODO), done_cb, t->id);
        on_click(small_button(acts, "Focus 25 min", false), focus_cb, t->id);
        on_click(small_button(acts, "Hand to Claude", false), claude_cb, t->id);
    } else {
        on_click(small_button(acts, "Not done after all", false), todo_cb, t->id);
    }
    if (t->planned) {
        if (index > 0) on_click(small_button(acts, LV_SYMBOL_UP, false), up_cb, t->id);
        on_click(small_button(acts, LV_SYMBOL_DOWN, false), down_cb, t->id);
        if (!done) on_click(small_button(acts, "Not today", false), snooze_cb, t->id);
        on_click(small_button(acts, "Off the list", false), remove_cb, t->id);
    }
    dk_anim_rise(acts, 0);
}

static void more_row(lv_obj_t *list, const dk_quest_t *t)
{
    lv_obj_t *r = dk_card(list, DK_CARD, 22);
    lv_obj_set_width(r, lv_pct(100));
    lv_obj_set_style_pad_hor(r, 16, 0);
    lv_obj_set_style_pad_ver(r, 10, 0);
    dk_row(r, 12);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (t->snoozed) lv_obj_set_style_opa(r, LV_OPA_60, 0);
    badge(r, t->src, 32);
    char line[180];
    snprintf(line, sizeof(line), "%s  %s%s", t->key, t->title, t->snoozed ? "  (not today)" : "");
    lv_obj_t *l = dk_label(r, DK_FONT_M, DK_INK, line);
    lv_obj_set_flex_grow(l, 1);
    lv_obj_set_width(l, 0);
    dk_one_line(l);
    if (t->boss) {
        lv_obj_t *tag = dk_pill(r, DK_ALERT_PILL, DK_FONT_XS, "Boss");
        lv_obj_set_style_text_color(tag, DK_ALERT_INK, 0);
    }
    lv_obj_t *b = small_button(r, "Add " LV_SYMBOL_PLUS, false);
    on_click(b, add_cb, t->id);
}

static void refresh_list(void)
{
    const dk_quests_t *Q = &g_model->quests;
    uint32_t sig = dk_hash(DK_HASH0, Q->tasks, sizeof(Q->tasks[0]) * (size_t)Q->task_n);
    sig = dk_hash(sig, &Q->task_n, sizeof(Q->task_n));
    sig = dk_hash_str(sig, q.open_id);
    bool armed = q.armed_id[0] && lv_tick_diff(lv_tick_get(), q.armed_at) < ARM_MS;
    sig = dk_hash_str(sig, armed ? q.armed_id : "");
    if (sig == q.list_sig) return;
    q.list_sig = sig;
    lv_coord_t y = lv_obj_get_scroll_y(q.list);
    lv_obj_clean(q.list);

    int planned = 0;
    for (int i = 0; i < Q->task_n; i++)
        if (Q->tasks[i].planned) quest_card(q.list, &Q->tasks[i], planned++);
    if (!planned) {
        lv_obj_t *l = dk_label(q.list, DK_FONT_M, DK_MUTED, "Nothing planned yet. Add a quest from below.");
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    }
    bool heading = false;
    for (int i = 0; i < Q->task_n; i++) {
        if (Q->tasks[i].planned) continue;
        if (!heading) {
            lv_obj_t *h = dk_heading(q.list, "MORE QUESTS");
            lv_obj_set_style_margin_top(h, 14, 0);
            heading = true;
        }
        more_row(q.list, &Q->tasks[i]);
    }
    lv_obj_update_layout(q.list);
    lv_obj_scroll_to_y(q.list, y, LV_ANIM_OFF);
}

static void refresh_week(void)
{
    const dk_quests_t *Q = &g_model->quests;
    uint32_t sig = dk_hash(DK_HASH0, Q->week, sizeof(Q->week[0]) * (size_t)Q->week_n);
    if (sig == q.week_sig) return;
    q.week_sig = sig;
    lv_obj_clean(q.week);
    for (int i = 0; i < Q->week_n; i++) {
        const dk_challenge_t *ch = &Q->week[i];
        lv_obj_t *r = dk_box(q.week);
        lv_obj_set_width(r, lv_pct(100));
        dk_col(r, 4);
        lv_obj_t *top = dk_box(r);
        lv_obj_set_width(top, lv_pct(100));
        dk_row(top, 8);
        lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *l = dk_label(top, DK_FONT_S, ch->done ? DK_GOOD_INK : DK_INK, ch->label);
        lv_obj_set_flex_grow(l, 1);
        lv_obj_set_width(l, 0);
        dk_one_line(l);
        char n[24];
        if (ch->done) snprintf(n, sizeof(n), LV_SYMBOL_OK);
        else if (ch->goal >= 60 && strstr(ch->label, "hours")) snprintf(n, sizeof(n), "%d / %dh", (int)(ch->have / 60), (int)(ch->goal / 60));
        else snprintf(n, sizeof(n), "%d / %d", (int)ch->have, (int)ch->goal);
        dk_label(top, ch->done ? &lv_font_montserrat_20 : DK_FONT_XS, ch->done ? DK_GOOD_INK : DK_MUTED, n);
        lv_obj_t *b = bar(r, lv_pct(100), 8, DK_LILAC_SOFT, ch->done ? DK_GOOD_INK : DK_PURPLE);
        lv_bar_set_range(b, 0, ch->goal > 0 ? ch->goal : 1);
        lv_bar_set_value(b, ch->have, LV_ANIM_OFF);
    }
}

static void refresh_panel(void)
{
    const dk_quests_t *Q = &g_model->quests;
    char t[64];
    snprintf(t, sizeof(t), "Level %d", (int)Q->level);
    dk_set_text(q.level, t);
    snprintf(t, sizeof(t), "%d / %d XP", (int)Q->level_xp, (int)Q->next_xp);
    dk_set_text(q.xp_text, t);
    int32_t max = Q->next_xp > 0 ? Q->next_xp : 100;
    lv_bar_set_range(q.xp_bar, 0, max);
    lv_arc_set_range(q.ring, 0, max);
    bool grew = Q->xp > q.shown_xp && q.shown_xp > 0;
    lv_bar_set_value(q.xp_bar, Q->level_xp, grew ? LV_ANIM_ON : LV_ANIM_OFF);
    lv_arc_set_value(q.ring, Q->level_xp);
    q.shown_xp = Q->xp;
    snprintf(t, sizeof(t), "%d day streak", (int)Q->streak);
    dk_set_text(lv_obj_get_child(q.streak, 1), Q->streak ? t : "No streak yet");
    lv_obj_t *cl = lv_obj_get_child(q.combo, 0);
    if (Q->combo >= 2) {
        snprintf(t, sizeof(t), "Combo x%d", (int)Q->combo);
        dk_set_text(cl, t);
        lv_obj_remove_flag(q.combo, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(q.combo, LV_OBJ_FLAG_HIDDEN);
    }
    snprintf(t, sizeof(t), "+%d XP today  ·  %d done", (int)Q->today_xp, (int)Q->today_done);
    dk_set_text(q.today, t);
    deskling_set_outfit(q.face, Q->outfits);
    refresh_week();
}

static void refresh_head(void)
{
    const dk_quests_t *Q = &g_model->quests;
    char t[64];
    if (Q->plan_total) snprintf(t, sizeof(t), Q->plan_done == Q->plan_total ? "All %d done. Day cleared!" : "%d of %d done", Q->plan_done, Q->plan_total);
    else snprintf(t, sizeof(t), "Pick a few to play today");
    dk_set_text(q.title_sub, t);
    // One segment per quest, filled as they're done.
    if ((int)lv_obj_get_child_count(q.segs) != Q->plan_total) {
        lv_obj_clean(q.segs);
        for (int i = 0; i < Q->plan_total && i < 12; i++) {
            lv_obj_t *s = dk_box(q.segs);
            lv_obj_set_size(s, 34, 12);
            lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
        }
    }
    for (int i = 0; i < (int)lv_obj_get_child_count(q.segs); i++)
        lv_obj_set_style_bg_color(lv_obj_get_child(q.segs, i), i < Q->plan_done ? DK_GOOD_INK : DK_LINE, 0);
    // Which services are connected, and whether they're well.
    lv_obj_clean(q.sources);
    for (int s = 0; s < 3; s++) {
        if (!(Q->connected & (1u << s))) continue;
        lv_obj_t *p = dk_card(q.sources, DK_CARD, LV_RADIUS_CIRCLE);
        lv_obj_set_style_pad_left(p, 6, 0);
        lv_obj_set_style_pad_right(p, 12, 0);
        lv_obj_set_style_pad_ver(p, 5, 0);
        dk_row(p, 8);
        badge(p, (dk_qsrc_t)s, 26);
        dk_label(p, DK_FONT_XS, DK_INK, dk_qsrc_label((dk_qsrc_t)s));
        dk_dot(p, 10, Q->failing & (1u << s) ? DK_ALERT_INK : DK_GOOD_INK, false);
    }
    if (Q->error[0]) {
        dk_set_text(q.error_text, Q->error);
        lv_obj_remove_flag(q.error, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(q.error, LV_OBJ_FLAG_HIDDEN);
    }
    bool none = !Q->connected;
    if (none) lv_obj_remove_flag(q.empty, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(q.empty, LV_OBJ_FLAG_HIDDEN);
}

// ------------------------------------------------------------ focus

static void refresh_focus(void)
{
    const dk_quests_t *Q = &g_model->quests;
    if (!Q->focus_id[0]) {
        lv_obj_add_flag(q.focus, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    bool was_hidden = lv_obj_has_flag(q.focus, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(q.focus, LV_OBJ_FLAG_HIDDEN);
    int64_t now = ui_now_ms() + ui_relay_offset_ms();
    int64_t left = Q->focus_ends - now;
    if (left < 0) left = 0;
    int total = Q->focus_minutes > 0 ? Q->focus_minutes * 60 : 1500;
    char t[16];
    snprintf(t, sizeof(t), "%02d:%02d", (int)(left / 60000), (int)(left / 1000 % 60));
    dk_set_text(q.focus_time, t);
    lv_arc_set_range(q.focus_ring, 0, total);
    lv_arc_set_value(q.focus_ring, (int32_t)(left / 1000));
    const dk_quest_t *task = dk_quest_find(g_model, Q->focus_id);
    dk_set_text(q.focus_key, task ? task->key : "");
    dk_set_text(q.focus_title, task ? task->title : "");
    if (task && task->boss && task->hp_max) {
        // The boss loses a point of HP for every minute of focus, live.
        int spent = (int)((now - Q->focus_started) / 60000);
        int hp = task->hp - spent;
        if (hp < 0) hp = 0;
        lv_bar_set_range(q.focus_hp, 0, task->hp_max);
        lv_bar_set_value(q.focus_hp, hp, LV_ANIM_ON);
        char h[32];
        snprintf(h, sizeof(h), "Boss HP %d / %d", hp, task->hp_max);
        dk_set_text(q.focus_hp_text, h);
        lv_obj_remove_flag(lv_obj_get_parent(q.focus_hp), LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(lv_obj_get_parent(q.focus_hp), LV_OBJ_FLAG_HIDDEN);
    }
    deskling_set_outfit(q.focus_face, Q->outfits | DK_OUTFIT_HEADPHONES);
    if (was_hidden) {
        deskling_set_mood(q.focus_face, DK_MOOD_CALM); // the ring is the motion here; a calm Deskling is cheap to draw
        dk_anim_rise(q.focus, 0);
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (lv_screen_active() != q.scr) return;
    if (g_model->quests.focus_id[0]) refresh_focus();
    // A check button that was tapped once goes back after 3 s.
    if (q.armed_id[0] && lv_tick_diff(lv_tick_get(), q.armed_at) >= ARM_MS) {
        q.armed_id[0] = '\0';
        refresh_list();
    }
    // The combo's time runs down.
    const dk_quests_t *Q = &g_model->quests;
    if (Q->combo >= 2 && Q->combo_until) {
        int64_t left = Q->combo_until - (ui_now_ms() + ui_relay_offset_ms());
        lv_bar_set_value(q.combo_bar, left > 0 ? (int32_t)(left / 1000) : 0, LV_ANIM_OFF);
        if (left <= 0) lv_obj_add_flag(q.combo, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *build_focus(lv_obj_t *scr)
{
    lv_obj_t *f = dk_box(scr);
    lv_obj_add_flag(f, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(f, -32, -28);
    lv_obj_set_size(f, DK_W, DK_H);
    lv_obj_set_style_bg_color(f, DK_NIGHT, 0);
    lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(f, 48, 0);
    dk_row(f, 56);
    lv_obj_set_flex_align(f, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(f, LV_OBJ_FLAG_CLICKABLE); // nothing behind it is tappable

    lv_obj_t *ringbox = dk_box(f);
    lv_obj_set_size(ringbox, 440, 440);
    q.focus_ring = lv_arc_create(ringbox);
    lv_obj_remove_style_all(q.focus_ring);
    lv_obj_remove_flag(q.focus_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(q.focus_ring, 440, 440);
    lv_arc_set_bg_angles(q.focus_ring, 0, 360);
    lv_arc_set_rotation(q.focus_ring, 270);
    lv_arc_set_mode(q.focus_ring, LV_ARC_MODE_NORMAL);
    lv_obj_set_style_arc_width(q.focus_ring, 22, LV_PART_MAIN);
    lv_obj_set_style_arc_color(q.focus_ring, DK_NIGHT_CARD, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(q.focus_ring, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(q.focus_ring, 22, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(q.focus_ring, DK_LILAC, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(q.focus_ring, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(q.focus_ring, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(q.focus_ring, LV_OPA_TRANSP, LV_PART_KNOB); // a progress ring, not a slider
    lv_obj_set_style_pad_all(q.focus_ring, 0, LV_PART_KNOB);
    q.focus_face = deskling_create(ringbox, 170, DK_FACE);
    lv_obj_align(q.focus_face, LV_ALIGN_CENTER, 0, -40);
    q.focus_time = dk_label(ringbox, DK_FONT_CLOCK, DK_NIGHT_TEXT, "25:00");
    lv_obj_align(q.focus_time, LV_ALIGN_CENTER, 0, 110);

    lv_obj_t *col = dk_box(f);
    lv_obj_set_width(col, 560);
    dk_col(col, 14);
    dk_label(col, DK_FONT_L, DK_NIGHT_MUTED, "Focusing on");
    q.focus_key = dk_label(col, DK_FONT_XL, DK_LILAC, "");
    q.focus_title = dk_label(col, DK_FONT_H2, DK_NIGHT_TEXT, "");
    lv_obj_set_width(q.focus_title, lv_pct(100));
    lv_label_set_long_mode(q.focus_title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_t *hp = dk_box(col);
    lv_obj_set_width(hp, lv_pct(100));
    dk_col(hp, 6);
    q.focus_hp_text = dk_label(hp, DK_FONT_S, DK_ALERT_RING, "");
    q.focus_hp = bar(hp, lv_pct(100), 16, DK_NIGHT_CARD, DK_ALERT_RING);
    lv_obj_t *btns = dk_box(col);
    lv_obj_set_width(btns, lv_pct(100));
    dk_row(btns, 14);
    lv_obj_set_style_margin_top(btns, 18, 0);
    lv_obj_t *d = dk_button(btns, "Done " LV_SYMBOL_OK, true, DK_FONT_L);
    lv_obj_set_style_bg_color(d, DK_LILAC, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(d, 0), DK_INK, 0);
    lv_obj_add_event_cb(d, focus_done_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *s = dk_button(btns, "Stop focusing", false, DK_FONT_L);
    lv_obj_set_style_bg_color(s, DK_NIGHT_CARD, 0);
    lv_obj_set_style_border_color(s, DK_NIGHT_MUTED, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(s, 0), DK_NIGHT_TEXT, 0);
    lv_obj_add_event_cb(s, focus_stop_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(f, LV_OBJ_FLAG_HIDDEN);
    return f;
}

lv_obj_t *scr_quests_create(void)
{
    q.scr = lv_obj_create(NULL);
    lv_obj_remove_flag(q.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(q.scr, DK_GROUND, 0);
    lv_obj_set_style_pad_all(q.scr, 28, 0);
    lv_obj_set_style_pad_hor(q.scr, 32, 0);
    dk_row(q.scr, 24);
    lv_obj_set_flex_align(q.scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // ---- left: Deskling's score
    lv_obj_t *left = dk_card(q.scr, DK_LILAC, 36);
    lv_obj_set_size(left, 340, lv_pct(100));
    lv_obj_set_style_pad_hor(left, 24, 0);
    lv_obj_set_style_pad_ver(left, 22, 0);
    dk_col(left, 10);
    lv_obj_add_flag(left, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(left, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(left, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *top = dk_box(left);
    lv_obj_set_width(top, lv_pct(100));
    dk_row(top, 8);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *back = lv_button_create(top);
    lv_obj_remove_style_all(back);
    lv_obj_set_style_bg_color(back, DK_CARD, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(back, 18, 0);
    lv_obj_set_height(back, 52);
    dk_row(back, 8);
    dk_label(back, &lv_font_montserrat_20, DK_INK, LV_SYMBOL_LEFT);
    dk_label(back, DK_FONT_S, DK_INK, "Home");
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    dk_press_style(back);

    // Deskling inside its level ring.
    lv_obj_t *fw = dk_box(left);
    lv_obj_set_size(fw, lv_pct(100), 210);
    dk_overflow(fw, 40);
    q.ring = lv_arc_create(fw);
    lv_obj_remove_style_all(q.ring);
    lv_obj_remove_flag(q.ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(q.ring, 200, 200);
    lv_obj_align(q.ring, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(q.ring, 0, 360);
    lv_arc_set_rotation(q.ring, 270);
    lv_obj_set_style_arc_width(q.ring, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_color(q.ring, DK_LILAC_SOFT, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(q.ring, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(q.ring, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(q.ring, DK_PURPLE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(q.ring, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(q.ring, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(q.ring, LV_OPA_TRANSP, LV_PART_KNOB); // a progress ring, not a slider
    lv_obj_set_style_pad_all(q.ring, 0, LV_PART_KNOB);
    q.face = deskling_create(fw, 140, DK_FACE);
    lv_obj_align(q.face, LV_ALIGN_CENTER, 0, 4);

    q.level = dk_label(left, DK_FONT_H2, DK_INK, "Level 1");
    q.xp_text = dk_label(left, DK_FONT_S, DK_INK_SOFT, "");
    q.xp_bar = bar(left, lv_pct(100), 16, DK_CARD, DK_PURPLE);

    lv_obj_t *chips = dk_box(left);
    lv_obj_set_width(chips, lv_pct(100));
    dk_row(chips, 8);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(chips, 8, 0);
    lv_obj_set_style_margin_top(chips, 4, 0);
    q.streak = dk_card(chips, DK_CARD, LV_RADIUS_CIRCLE);
    lv_obj_set_style_pad_hor(q.streak, 12, 0);
    lv_obj_set_style_pad_ver(q.streak, 6, 0);
    dk_row(q.streak, 8);
    lv_obj_t *flame = dk_box(q.streak);
    lv_obj_set_size(flame, 14, 18);
    lv_obj_set_style_radius(flame, 7, 0);
    lv_obj_set_style_bg_color(flame, DK_ALERT_RING, 0);
    lv_obj_set_style_bg_opa(flame, LV_OPA_COVER, 0);
    dk_anim_breathe_opa(flame, LV_OPA_50, LV_OPA_COVER, 1400, 0);
    dk_label(q.streak, DK_FONT_XS, DK_INK, "");
    q.combo = dk_card(chips, DK_BUTTER, LV_RADIUS_CIRCLE);
    lv_obj_set_style_pad_hor(q.combo, 12, 0);
    lv_obj_set_style_pad_ver(q.combo, 6, 0);
    dk_col(q.combo, 3);
    dk_label(q.combo, DK_FONT_XS, DK_BUTTER_INK, "");
    q.combo_bar = bar(q.combo, 80, 4, DK_BUTTER_BG, DK_BUTTER_INK);
    lv_bar_set_range(q.combo_bar, 0, 45 * 60);
    lv_obj_add_flag(q.combo, LV_OBJ_FLAG_HIDDEN);
    q.today = dk_label(left, DK_FONT_S, DK_INK_SOFT, "");

    lv_obj_t *wk = dk_card(left, DK_CARD, 22);
    lv_obj_set_width(wk, lv_pct(100));
    lv_obj_set_style_pad_all(wk, 16, 0);
    lv_obj_set_style_margin_top(wk, 6, 0);
    dk_col(wk, 10);
    dk_heading(wk, "THIS WEEK");
    q.week = dk_box(wk);
    lv_obj_set_width(q.week, lv_pct(100));
    dk_col(q.week, 12);

    // ---- right: today's quests
    lv_obj_t *right = dk_box(q.scr);
    lv_obj_set_height(right, lv_pct(100));
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_width(right, 0);
    dk_col(right, 12);
    lv_obj_t *head = dk_box(right);
    lv_obj_set_width(head, lv_pct(100));
    dk_row(head, 16);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *ht = dk_box(head);
    lv_obj_set_flex_grow(ht, 1);
    dk_col(ht, 6);
    dk_label(ht, DK_FONT_H2, DK_INK, "Today's quests");
    lv_obj_t *sub = dk_box(ht);
    dk_row(sub, 14);
    lv_obj_set_flex_align(sub, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    q.title_sub = dk_label(sub, DK_FONT_M, DK_INK_SOFT, "");
    q.segs = dk_box(sub);
    dk_row(q.segs, 6);
    q.sources = dk_box(head);
    dk_row(q.sources, 8);
    lv_obj_t *sy = lv_button_create(head);
    lv_obj_remove_style_all(sy);
    lv_obj_set_size(sy, 52, 52);
    lv_obj_set_style_radius(sy, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sy, DK_CARD, 0);
    lv_obj_set_style_bg_opa(sy, LV_OPA_COVER, 0);
    lv_obj_t *syl = dk_label(sy, &lv_font_montserrat_20, DK_INK, LV_SYMBOL_REFRESH);
    lv_obj_center(syl);
    lv_obj_add_event_cb(sy, sync_cb, LV_EVENT_CLICKED, NULL);
    dk_press_style(sy);

    q.error = dk_pill(right, DK_ALERT_BG, DK_FONT_XS, "");
    q.error_text = lv_obj_get_child(q.error, 0);
    lv_obj_set_style_text_color(q.error_text, DK_ALERT_INK, 0);
    lv_obj_set_style_max_width(q.error_text, 820, 0);
    lv_label_set_long_mode(q.error_text, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_add_flag(q.error, LV_OBJ_FLAG_HIDDEN);

    q.list = dk_box(right);
    lv_obj_set_width(q.list, lv_pct(100));
    lv_obj_set_flex_grow(q.list, 1);
    dk_col(q.list, 12);
    lv_obj_set_style_pad_bottom(q.list, 24, 0);
    lv_obj_add_flag(q.list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(q.list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(q.list, LV_SCROLLBAR_MODE_OFF);

    // Nothing connected yet: say where to do it.
    q.empty = dk_card(right, DK_CARD, 30);
    lv_obj_add_flag(q.empty, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(q.empty, lv_pct(100), 360);
    lv_obj_align(q.empty, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_pad_all(q.empty, 36, 0);
    dk_col(q.empty, 12);
    lv_obj_set_flex_align(q.empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *ef = deskling_create(q.empty, 120, DK_FACE);
    deskling_set_mood(ef, DK_MOOD_CURIOUS);
    dk_label(q.empty, DK_FONT_XL, DK_INK, "Turn your tickets into quests");
    lv_obj_t *el = dk_label(q.empty, DK_FONT_M, DK_INK_SOFT,
                            "Connect Mojo Desk, Linear or Jira in Deskling for your browser: open /app on your relay, then Quests, then Connect. "
                            "Your assigned tickets show up here with XP, bosses, streaks and combos.");
    lv_obj_set_width(el, 680);
    lv_label_set_long_mode(el, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(el, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(q.empty, LV_OBJ_FLAG_HIDDEN);

    q.focus = build_focus(q.scr);
    q.tick = lv_timer_create(tick_cb, 1000, NULL);
    return q.scr;
}

void scr_quests_refresh(void)
{
    if (!q.scr) return;
    refresh_panel();
    refresh_head();
    refresh_list();
    refresh_focus();
    // Only on a change (setting a mood restarts its motion), and never cutting a celebration short.
    dk_mood_t want = g_model->quests.focus_id[0] ? DK_MOOD_BUSY : g_model->quests.cleared ? DK_MOOD_HAPPY : DK_MOOD_CALM;
    dk_mood_t now = deskling_get_mood(q.face);
    if (now != want && (now != DK_MOOD_HAPPY || want == DK_MOOD_BUSY)) deskling_set_mood(q.face, want);
}

bool scr_quests_back(void)
{
    if (q.open_id[0]) {
        q.open_id[0] = '\0';
        refresh_list();
        return true;
    }
    return false;
}
