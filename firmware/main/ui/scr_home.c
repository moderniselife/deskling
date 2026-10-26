// Home: Deskling + the lead agent, agents list, clock, reminders, live feed, quick actions.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_internal.h"

static struct {
    lv_obj_t *scr;
    lv_obj_t *face, *status_dot_wrap, *status_label, *status_pill, *headline, *subline, *eq, *meta;
    lv_obj_t *agents_list;
    lv_obj_t *hh, *mm, *ampm, *date, *conn_dot, *conn_label;
    lv_obj_t *rem_list, *rem_count;
    lv_obj_t *feed_list;
    lv_obj_t *focus_btn;
    char last_feed_top[16];
    bool cheering;
} h;

static void open_sessions(lv_event_t *e)
{
    (void)e;
    ui_go(SCR_SESSIONS);
}

static void open_settings(lv_event_t *e)
{
    (void)e;
    ui_go(SCR_SETTINGS);
}

static void open_agent(lv_event_t *e)
{
    const char *id = lv_event_get_user_data(e);
    ui_open_chat(id);
}

static void action_cb(lv_event_t *e)
{
    const char *name = lv_event_get_user_data(e);
    dk_send_action(name);
    if (strcmp(name, "ping_when_done") == 0) deskling_celebrate(h.face, 1200);
}

static void reminder_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_current_target(e);
    lv_obj_t *chk = lv_obj_get_child(row, 0);
    const char *id = lv_event_get_user_data(e);
    bool now_done = !lv_obj_has_state(row, LV_STATE_CHECKED);
    // Optimistic: animate straight away, the relay's snapshot confirms it.
    if (now_done) {
        lv_obj_add_state(row, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(chk, DK_GOOD_INK, 0);
        lv_obj_set_style_border_color(chk, DK_GOOD_INK, 0);
        lv_obj_t *mark = dk_label(chk, &lv_font_montserrat_20, lv_color_white(), LV_SYMBOL_OK);
        lv_obj_center(mark);
        dk_anim_pop(mark, 0);
        deskling_celebrate(h.face, 1800);
        lv_obj_set_style_opa(lv_obj_get_child(row, 1), LV_OPA_60, 0);
    }
    dk_send_reminder_done(id, now_done);
}

// --------------------------------------------------------------- build

lv_obj_t *scr_home_create(void)
{
    h.scr = lv_obj_create(NULL);
    lv_obj_remove_flag(h.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(h.scr, DK_GROUND, 0);
    lv_obj_set_style_pad_all(h.scr, 32, 0);
    dk_row(h.scr, 24);
    lv_obj_set_flex_align(h.scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // ---- left column
    lv_obj_t *left = dk_box(h.scr);
    lv_obj_set_size(left, 440, lv_pct(100));
    dk_col(left, 24);

    lv_obj_t *buddy = dk_card(left, DK_LILAC, 36);
    lv_obj_set_width(buddy, lv_pct(100));
    lv_obj_set_flex_grow(buddy, 1);
    lv_obj_set_style_pad_hor(buddy, 32, 0);
    lv_obj_set_style_pad_ver(buddy, 26, 0);
    dk_col(buddy, 10);
    dk_anim_rise(buddy, 0);

    lv_obj_t *top = dk_box(buddy);
    lv_obj_set_width(top, lv_pct(100));
    dk_row(top, 8);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    dk_label(top, DK_FONT_L, DK_INK, "deskling");
    h.status_pill = dk_card(top, DK_CARD, LV_RADIUS_CIRCLE);
    lv_obj_set_style_pad_hor(h.status_pill, 14, 0);
    lv_obj_set_style_pad_ver(h.status_pill, 6, 0);
    dk_row(h.status_pill, 8);
    h.status_dot_wrap = dk_box(h.status_pill);
    h.status_label = dk_label(h.status_pill, DK_FONT_XS, DK_INK, "Quiet");

    lv_obj_t *face_wrap = dk_box(buddy);
    lv_obj_set_size(face_wrap, lv_pct(100), 178);
    dk_overflow(face_wrap, 60);
    h.face = deskling_create(face_wrap, 168, DK_FACE);
    lv_obj_align(h.face, LV_ALIGN_CENTER, 0, 4);

    h.headline = dk_label(buddy, DK_FONT_H2, DK_INK, "All quiet");
    lv_obj_set_width(h.headline, lv_pct(100));
    lv_label_set_long_mode(h.headline, LV_LABEL_LONG_MODE_DOTS);
    h.subline = dk_label(buddy, DK_FONT_M, DK_INK_SOFT, "");
    lv_obj_set_size(h.subline, lv_pct(100), 54);
    lv_label_set_long_mode(h.subline, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *bottom = dk_box(buddy);
    lv_obj_set_width(bottom, lv_pct(100));
    dk_row(bottom, 14);
    h.eq = ui_eq_bars(bottom, 5, 26, DK_INK);
    h.meta = dk_label(bottom, DK_FONT_S, DK_INK_SOFT, "");

    lv_obj_t *agents = dk_card(left, DK_CARD, 32);
    lv_obj_set_width(agents, lv_pct(100));
    lv_obj_set_style_pad_hor(agents, 24, 0);
    lv_obj_set_style_pad_ver(agents, 20, 0);
    dk_col(agents, 6);
    dk_anim_rise(agents, 120);
    lv_obj_t *ah = dk_box(agents);
    lv_obj_set_width(ah, lv_pct(100));
    dk_row(ah, 8);
    lv_obj_set_flex_align(ah, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    dk_heading(ah, "AGENTS");
    lv_obj_t *all = lv_button_create(ah);
    lv_obj_remove_style_all(all);
    lv_obj_set_style_pad_hor(all, 12, 0);
    lv_obj_set_style_pad_ver(all, 8, 0);
    dk_label(all, DK_FONT_S, DK_PURPLE, "See all " LV_SYMBOL_RIGHT);
    lv_obj_add_event_cb(all, open_sessions, LV_EVENT_CLICKED, NULL);
    dk_press_style(all);
    h.agents_list = dk_box(agents);
    lv_obj_set_width(h.agents_list, lv_pct(100));
    dk_col(h.agents_list, 0);

    // ---- right column
    lv_obj_t *right = dk_box(h.scr);
    lv_obj_set_height(right, lv_pct(100));
    lv_obj_set_flex_grow(right, 1);
    dk_col(right, 24);

    lv_obj_t *header = dk_box(right);
    lv_obj_set_size(header, lv_pct(100), 96);
    dk_row(header, 8);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    dk_anim_rise(header, 50);
    lv_obj_t *clock = dk_box(header);
    dk_row(clock, 0);
    lv_obj_set_flex_align(clock, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    h.hh = dk_label(clock, DK_FONT_CLOCK, DK_INK, "12");
    lv_obj_t *colon = dk_label(clock, DK_FONT_CLOCK, DK_INK, ":");
    dk_anim_blink(colon);
    h.mm = dk_label(clock, DK_FONT_CLOCK, DK_INK, "00");
    h.ampm = dk_label(clock, DK_FONT_XL, DK_INK, "pm");
    lv_obj_set_style_pad_left(h.ampm, 8, 0);
    lv_obj_set_style_pad_bottom(h.ampm, 10, 0);
    lv_obj_t *hr = dk_box(header);
    dk_col(hr, 6);
    lv_obj_set_flex_align(hr, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    h.date = dk_label(hr, DK_FONT_L, DK_INK, "");
    lv_obj_t *conn = lv_button_create(hr);
    lv_obj_remove_style_all(conn);
    lv_obj_set_size(conn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(conn, 6, 0);
    lv_obj_set_ext_click_area(conn, 16);
    dk_row(conn, 8);
    lv_obj_add_event_cb(conn, open_settings, LV_EVENT_CLICKED, NULL);
    dk_press_style(conn);
    h.conn_dot = dk_dot(conn, 10, DK_MUTED, false);
    h.conn_label = dk_label(conn, DK_FONT_S, DK_MUTED, "Connecting…");
    dk_label(conn, &lv_font_montserrat_14, DK_MUTED, LV_SYMBOL_SETTINGS);

    lv_obj_t *mid = dk_box(right);
    lv_obj_set_width(mid, lv_pct(100));
    lv_obj_set_flex_grow(mid, 1);
    dk_row(mid, 24);
    lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *rem = dk_card(mid, DK_CARD, 32);
    lv_obj_set_height(rem, lv_pct(100));
    lv_obj_set_flex_grow(rem, 1);
    lv_obj_set_width(rem, 0);
    lv_obj_set_style_pad_hor(rem, 24, 0);
    lv_obj_set_style_pad_ver(rem, 20, 0);
    dk_col(rem, 6);
    dk_anim_rise(rem, 200);
    lv_obj_t *rh = dk_box(rem);
    lv_obj_set_width(rh, lv_pct(100));
    dk_row(rh, 8);
    lv_obj_set_flex_align(rh, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    dk_heading(rh, "REMINDERS");
    h.rem_count = dk_pill(rh, DK_WARM, DK_FONT_XS, "");
    h.rem_list = dk_box(rem);
    lv_obj_set_width(h.rem_list, lv_pct(100));
    dk_col(h.rem_list, 0);

    lv_obj_t *feed = dk_card(mid, DK_CARD, 32);
    lv_obj_set_height(feed, lv_pct(100));
    lv_obj_set_flex_grow(feed, 1);
    lv_obj_set_width(feed, 0);
    lv_obj_set_style_pad_hor(feed, 24, 0);
    lv_obj_set_style_pad_ver(feed, 20, 0);
    lv_obj_set_style_clip_corner(feed, true, 0);
    dk_col(feed, 6);
    dk_anim_rise(feed, 280);
    lv_obj_t *fh = dk_box(feed);
    lv_obj_set_width(fh, lv_pct(100));
    dk_row(fh, 8);
    lv_obj_set_flex_align(fh, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    dk_heading(fh, "FEED");
    lv_obj_t *live = dk_box(fh);
    dk_row(live, 6);
    dk_dot(live, 8, DK_GOOD_INK, true);
    dk_label(live, DK_FONT_XS, DK_MUTED, "Live");
    h.feed_list = dk_box(feed);
    lv_obj_set_width(h.feed_list, lv_pct(100));
    dk_col(h.feed_list, 0);

    lv_obj_t *actions = dk_box(right);
    lv_obj_set_size(actions, lv_pct(100), 72);
    dk_row(actions, 16);
    dk_anim_rise(actions, 360);
    static const char *names[] = {"focus", "snooze_all", "ping_when_done"};
    static const char *labels[] = {"Focus mode", "Snooze all 15 min", "Ping me when done"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = dk_button(actions, labels[i], i == 0, DK_FONT_M);
        lv_obj_set_height(b, 72);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_add_event_cb(b, action_cb, LV_EVENT_CLICKED, (void *)names[i]);
        if (i == 0) h.focus_btn = b;
    }

    scr_home_tick();
    scr_home_refresh(DK_CAUSE_HELLO, "");
    return h.scr;
}

// ------------------------------------------------------------- refresh

static void build_agents(void)
{
    // Rebuild only when what the list shows has changed (snapshots arrive for all sorts of reasons).
    static uint32_t sig;
    int n = g_model->agent_n < 3 ? g_model->agent_n : 3;
    uint32_t now = dk_hash(dk_hash(DK_HASH0, &g_model->agent_n, sizeof(g_model->agent_n)), g_model->agents, n * sizeof(g_model->agents[0]));
    if (now == sig) return;
    sig = now;
    lv_obj_clean(h.agents_list);
    if (g_model->agent_n == 0) {
        dk_label(h.agents_list, DK_FONT_M, DK_MUTED, "No sessions yet. Start Claude Code and it shows up here.");
        lv_obj_set_width(lv_obj_get_child(h.agents_list, 0), lv_pct(100));
        lv_label_set_long_mode(lv_obj_get_child(h.agents_list, 0), LV_LABEL_LONG_MODE_WRAP);
        return;
    }
    for (int i = 0; i < g_model->agent_n && i < 3; i++) {
        const dk_agent_t *a = &g_model->agents[i];
        lv_obj_t *row = lv_button_create(h.agents_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), 48);
        dk_row(row, 12);
        lv_obj_add_event_cb(row, open_agent, LV_EVENT_CLICKED, (void *)a->id);
        dk_press_style(row);
        dk_dot(row, 12, dk_state_color(a->state), a->state == DK_WORKING || a->state == DK_NEEDS_YOU);
        char buf[120];
        snprintf(buf, sizeof(buf), "%s #6A6274 · %s#", a->name, a->host);
        lv_obj_t *n = dk_label(row, DK_FONT_M, DK_INK, buf);
        lv_label_set_recolor(n, true);
        lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_flex_grow(n, 1);
        lv_obj_t *pill = dk_pill(row, dk_state_pill(a->state), DK_FONT_XS, dk_state_label(a->state));
        if (a->state == DK_NEEDS_YOU) dk_anim_wiggle(pill);
    }
}

static const char *feed_icon(dk_level_t l)
{
    switch (l) {
    case DK_FEED_ALERT: return LV_SYMBOL_WARNING;
    case DK_FEED_GOOD: return LV_SYMBOL_OK;
    case DK_FEED_REMINDER: return LV_SYMBOL_BELL;
    default: return LV_SYMBOL_BELL;
    }
}

static lv_color_t feed_tint(dk_level_t l)
{
    switch (l) {
    case DK_FEED_ALERT: return DK_ALERT_PILL;
    case DK_FEED_GOOD: return DK_GOOD_BG;
    case DK_FEED_REMINDER: return DK_BUTTER;
    default: return DK_LILAC_SOFT;
    }
}

static void feed_open(lv_event_t *e)
{
    const char *agent = lv_event_get_user_data(e);
    if (agent && dk_model_find_agent(g_model, agent)) ui_open_chat(agent);
}

static void feed_free(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

static void build_feed(void)
{
    static uint32_t sig;
    int n = g_model->feed_n < 4 ? g_model->feed_n : 4;
    uint32_t now = dk_hash(dk_hash(DK_HASH0, &g_model->feed_n, sizeof(g_model->feed_n)), g_model->feed, n * sizeof(g_model->feed[0]));
    for (int i = 0; i < n; i++) {
        char ago[12];
        ui_ago(g_model->feed[i].ts, ago, sizeof(ago));
        now = dk_hash_str(now, ago);
    }
    if (now == sig) return;
    sig = now;
    bool fresh = g_model->feed_n && strcmp(g_model->feed[0].id, h.last_feed_top) != 0 && h.last_feed_top[0];
    snprintf(h.last_feed_top, sizeof(h.last_feed_top), "%s", g_model->feed_n ? g_model->feed[0].id : "-");
    lv_obj_clean(h.feed_list);
    if (g_model->feed_n == 0) {
        dk_label(h.feed_list, DK_FONT_M, DK_MUTED, "Quiet so far.");
        return;
    }
    for (int i = 0; i < g_model->feed_n && i < 4; i++) {
        const dk_feed_t *f = &g_model->feed[i];
        lv_obj_t *row = dk_box(h.feed_list);
        lv_obj_set_size(row, lv_pct(100), 66);
        if (f->agent_id[0]) {
            // Tap a notification about a session to open its chat.
            char *a = strdup(f->agent_id);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row, feed_open, LV_EVENT_CLICKED, a);
            lv_obj_add_event_cb(row, feed_free, LV_EVENT_DELETE, a);
            dk_press_style(row);
        }
        dk_row(row, 14);
        lv_obj_t *icon = dk_card(row, feed_tint(f->level), 16);
        lv_obj_set_size(icon, 44, 44);
        lv_obj_set_style_pad_all(icon, 0, 0);
        lv_obj_t *il = dk_label(icon, &lv_font_montserrat_20, DK_INK, feed_icon(f->level));
        lv_obj_center(il);
        lv_obj_t *txt = dk_box(row);
        dk_col(txt, 0);
        lv_obj_set_flex_grow(txt, 1);
        lv_obj_set_width(txt, 0);
        lv_obj_t *t = dk_label(txt, DK_FONT_M, DK_INK, f->title);
        lv_obj_set_width(t, lv_pct(100));
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_t *b = dk_label(txt, DK_FONT_XS, DK_MUTED, f->body);
        lv_obj_set_width(b, lv_pct(100));
        lv_label_set_long_mode(b, LV_LABEL_LONG_MODE_DOTS);
        char ago[12];
        ui_ago(f->ts, ago, sizeof(ago));
        dk_label(row, DK_FONT_XS, strcmp(ago, "now") == 0 ? DK_GOOD_INK : DK_MUTED, ago);
        if (fresh) {
            if (i == 0) dk_anim_pop(row, 120);
            else dk_anim_drop_in(row, -66, 0);
        }
    }
}

static void build_reminders(void)
{
    static uint32_t sig;
    int rn = g_model->reminder_n < 4 ? g_model->reminder_n : 4;
    uint32_t now = dk_hash(dk_hash(DK_HASH0, &g_model->reminder_n, sizeof(g_model->reminder_n)), g_model->reminders,
                           g_model->reminder_n * sizeof(g_model->reminders[0]));
    for (int i = 0; i < rn; i++) {
        char due[40];
        ui_due_text(g_model->reminders[i].due, due, sizeof(due));
        now = dk_hash_str(now, due);
    }
    if (now == sig) return;
    sig = now;
    lv_obj_clean(h.rem_list);
    int left = 0;
    for (int i = 0; i < g_model->reminder_n; i++) left += !g_model->reminders[i].done;
    lv_label_set_text_fmt(lv_obj_get_child(h.rem_count, 0), left ? "%d left" : "All done", left);
    if (g_model->reminder_n == 0) {
        lv_obj_t *l = dk_label(h.rem_list, DK_FONT_M, DK_MUTED, "Nothing to remember. Ask Claude to remind you of something!");
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        return;
    }
    for (int i = 0; i < g_model->reminder_n && i < 4; i++) {
        const dk_reminder_t *r = &g_model->reminders[i];
        // The whole row is the button; the circle is just the visual.
        lv_obj_t *row = lv_button_create(h.rem_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), 68);
        dk_row(row, 14);
        if (r->done) lv_obj_add_state(row, LV_STATE_CHECKED);
        lv_obj_add_event_cb(row, reminder_cb, LV_EVENT_CLICKED, (void *)r->id);
        lv_obj_t *chk = dk_box(row);
        lv_obj_set_size(chk, 44, 44);
        lv_obj_set_style_radius(chk, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(chk, 3, 0);
        lv_obj_set_style_border_color(chk, r->done ? DK_GOOD_INK : DK_INK, 0);
        lv_obj_set_style_bg_opa(chk, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(chk, r->done ? DK_GOOD_INK : DK_CARD, 0);
        if (r->done) {
            lv_obj_t *c = dk_label(chk, &lv_font_montserrat_20, lv_color_white(), LV_SYMBOL_OK);
            lv_obj_center(c);
        }
        dk_press_style(chk);
        lv_obj_t *txt = dk_box(row);
        dk_col(txt, 0);
        lv_obj_set_flex_grow(txt, 1);
        lv_obj_set_width(txt, 0);
        lv_obj_t *t = dk_label(txt, DK_FONT_M, DK_INK, r->title);
        lv_obj_set_width(t, lv_pct(100));
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        char due[40], meta[96];
        ui_due_text(r->due, due, sizeof(due));
        snprintf(meta, sizeof(meta), "%s · from %s", due, r->from);
        dk_label(txt, DK_FONT_XS, DK_MUTED, meta);
        if (r->done) {
            lv_obj_set_style_opa(txt, LV_OPA_60, 0);
            lv_obj_set_style_text_decor(t, LV_TEXT_DECOR_STRIKETHROUGH, 0);
        }
    }
}

static void build_companion(dk_cause_t cause)
{
    const dk_agent_t *a = ui_lead_agent();
    char buf[160];
    dk_set_text(h.headline, ui_headline_for(a, buf, sizeof(buf)));

    if (a && a->task[0]) {
        snprintf(buf, sizeof(buf), "%s · %s", a->task, a->name);
        dk_set_text(h.subline, buf);
    } else {
        dk_set_text(h.subline, g_model->agent_n ? "" : "Start a Claude Code session and I'll keep an eye on it.");
    }

    if (a) {
        char since[32];
        ui_since(a->since, since, sizeof(since));
        snprintf(buf, sizeof(buf), "%s · %s", a->host, since);
        dk_set_text(h.meta, buf);
    } else {
        dk_set_text(h.meta, "");
    }
    if (a && a->state == DK_WORKING) lv_obj_remove_flag(h.eq, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(h.eq, LV_OBJ_FLAG_HIDDEN);

    // Status pill + dot (rebuilt only when its colour or pulse changes, so the pulse keeps going).
    lv_color_t c = a ? dk_state_color(a->state) : DK_MUTED;
    bool ping = a && (a->state == DK_WORKING || a->state == DK_NEEDS_YOU);
    static uint32_t dot_sig;
    uint32_t dsig = dk_hash(dk_hash(DK_HASH0, &c, sizeof(c)), &ping, sizeof(ping));
    if (dsig != dot_sig || !lv_obj_get_child_count(h.status_dot_wrap)) {
        dot_sig = dsig;
        lv_obj_clean(h.status_dot_wrap);
        dk_dot(h.status_dot_wrap, 10, c, ping);
    }
    dk_set_text(h.status_label, a ? dk_state_label(a->state) : "Quiet");

    dk_mood_t mood = g_model->pending_n ? DK_MOOD_WORRIED : ui_mood_for(a);
    bool happy_cause = cause == DK_CAUSE_DONE || cause == DK_CAUSE_PING || cause == DK_CAUSE_REMINDER_DONE;
    if (happy_cause) {
        lv_obj_t *f = h.face;
        if (deskling_get_mood(f) != mood) deskling_set_mood(f, mood);
        deskling_celebrate(f, 2200);
    } else if (deskling_get_mood(h.face) != mood && deskling_get_mood(h.face) != DK_MOOD_HAPPY) {
        deskling_set_mood(h.face, mood);
    }
}

static void build_connection(void)
{
    lv_obj_t *dot_parent = lv_obj_get_parent(h.conn_dot);
    lv_obj_delete(h.conn_dot);
    lv_color_t c = g_conn == DK_CONN_ONLINE ? DK_GOOD_INK : DK_ALERT_INK;
    h.conn_dot = dk_dot(dot_parent, 10, c, g_conn != DK_CONN_ONLINE);
    lv_obj_move_to_index(h.conn_dot, 0);
    const char *text = g_conn_detail[0] ? g_conn_detail
                       : g_conn == DK_CONN_ONLINE ? "Online"
                       : g_conn == DK_CONN_RELAY  ? "Connecting to relay…"
                       : g_conn == DK_CONN_SETUP  ? "Needs Wi-Fi setup"
                                                  : "Joining Wi-Fi…";
    lv_label_set_text(h.conn_label, text);
}

void scr_home_refresh(dk_cause_t cause, const char *cause_id)
{
    (void)cause_id;
    if (!h.scr) return;
    build_companion(cause);
    build_agents();
    build_reminders();
    build_feed();
    build_connection();
    bool focus = g_model->focus;
    lv_obj_t *fl = lv_obj_get_child(h.focus_btn, 0);
    dk_set_text(fl, focus ? "Focus is on" : "Focus mode");
    lv_obj_set_style_bg_color(h.focus_btn, focus ? DK_PURPLE : DK_INK, 0);
}

void scr_home_tick(void)
{
    if (!h.scr) return;
    char hh[4], mm[4], ampm[4], date[48];
    ui_clock_parts(hh, mm, ampm, date, sizeof(date));
    lv_label_set_text(h.hh, hh);
    lv_label_set_text(h.mm, mm);
    lv_label_set_text(h.ampm, ampm);
    lv_label_set_text(h.date, date);
}
