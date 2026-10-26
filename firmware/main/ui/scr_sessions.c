// Sessions: every agent on every machine, as cards. Tap one to open its chat.
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

typedef enum { F_ALL, F_YOU, F_WORKING, F_DONE } filter_t;

static struct {
    lv_obj_t *scr, *rail, *subtitle, *grid, *tabs[4];
    filter_t filter;
    int last_count;
} s;

static bool matches(const dk_agent_t *a)
{
    switch (s.filter) {
    case F_YOU: return a->state == DK_NEEDS_YOU || a->state == DK_WAITING;
    case F_WORKING: return a->state == DK_WORKING;
    case F_DONE: return a->state == DK_DONE || a->state == DK_IDLE;
    default: return true;
    }
}

static void open_cb(lv_event_t *e)
{
    ui_open_chat((const char *)lv_event_get_user_data(e));
}

static void tab_cb(lv_event_t *e)
{
    s.filter = (filter_t)(intptr_t)lv_event_get_user_data(e);
    s.last_count = -1; // re-run the entrance animation
    scr_sessions_refresh();
}

static void new_cb(lv_event_t *e)
{
    (void)e;
    ui_go(SCR_NEW);
}

lv_obj_t *scr_sessions_create(void)
{
    s.scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s.scr, DK_GROUND, 0);
    lv_obj_set_style_pad_top(s.scr, 28, 0);
    lv_obj_set_style_pad_bottom(s.scr, 28, 0);
    lv_obj_set_style_pad_left(s.scr, 0, 0);
    lv_obj_set_style_pad_right(s.scr, 32, 0);
    dk_row(s.scr, 24);
    lv_obj_set_flex_align(s.scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s.rail = ui_nav_rail(s.scr, SCR_SESSIONS);

    lv_obj_t *main = dk_box(s.scr);
    lv_obj_set_height(main, lv_pct(100));
    lv_obj_set_flex_grow(main, 1);
    dk_col(main, 20);

    lv_obj_t *header = dk_box(main);
    lv_obj_set_width(header, lv_pct(100));
    dk_row(header, 16);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *titles = dk_box(header);
    dk_col(titles, 0);
    dk_label(titles, DK_FONT_H2, DK_INK, "Sessions");
    s.subtitle = dk_label(titles, DK_FONT_M, DK_MUTED, "");

    lv_obj_t *right = dk_box(header);
    dk_row(right, 14);
    lv_obj_t *nb = dk_button(right, "+  New session", true, DK_FONT_S);
    lv_obj_set_style_radius(nb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_height(nb, 60);
    lv_obj_add_event_cb(nb, new_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *tabs = dk_card(right, DK_CARD, LV_RADIUS_CIRCLE);
    lv_obj_set_style_pad_all(tabs, 6, 0);
    dk_row(tabs, 8);
    static const char *labels[] = {"All", "Needs you", "Working", "Done"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = lv_button_create(tabs);
        lv_obj_remove_style_all(b);
        lv_obj_set_height(b, 48);
        lv_obj_set_style_pad_hor(b, 20, 0);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(b, DK_INK, 0);
        lv_obj_t *l = dk_label(b, DK_FONT_S, DK_INK, labels[i]);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, tab_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        dk_press_style(b);
        s.tabs[i] = b;
    }

    s.grid = dk_box(main);
    lv_obj_set_width(s.grid, lv_pct(100));
    lv_obj_set_flex_grow(s.grid, 1);
    lv_obj_add_flag(s.grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s.grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s.grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s.grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(s.grid, 18, 0);
    lv_obj_set_style_pad_column(s.grid, 18, 0);

    s.last_count = -1;
    scr_sessions_refresh();
    return s.scr;
}

static void card(const dk_agent_t *a, int index, bool animate)
{
    lv_color_t bg = a->state == DK_NEEDS_YOU ? DK_ALERT_BG : a->state == DK_WAITING ? DK_BUTTER_BG : DK_CARD;
    lv_obj_t *c = lv_button_create(s.grid);
    lv_obj_remove_style_all(c);
    lv_obj_set_style_bg_color(c, bg, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 30, 0);
    lv_obj_set_style_pad_hor(c, 22, 0);
    lv_obj_set_style_pad_ver(c, 20, 0);
    // Three per row: (width - 2 gaps) / 3
    lv_obj_set_size(c, (DK_W - 104 - 24 - 32 - 36) / 3, 300);
    dk_col(c, 10);
    lv_obj_add_event_cb(c, open_cb, LV_EVENT_CLICKED, (void *)a->id);
    dk_press_style(c);

    lv_obj_t *top = dk_box(c);
    lv_obj_set_width(top, lv_pct(100));
    dk_row(top, 8);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_source_badge(top, a->source);
    dk_dot(top, 14, dk_state_color(a->state), a->state == DK_WORKING || a->state == DK_NEEDS_YOU);

    lv_obj_t *names = dk_box(c);
    lv_obj_set_width(names, lv_pct(100));
    dk_col(names, 0);
    lv_obj_t *n = dk_label(names, DK_FONT_XL, DK_INK, a->name);
    lv_obj_set_width(n, lv_pct(100));
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    dk_label(names, DK_FONT_S, DK_INK_SOFT, a->host);

    lv_obj_t *task = dk_label(c, DK_FONT_M, DK_INK, a->task[0] ? a->task : a->last_message);
    lv_obj_set_width(task, lv_pct(100));
    lv_obj_set_flex_grow(task, 1);
    lv_label_set_long_mode(task, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *bottom = dk_box(c);
    lv_obj_set_width(bottom, lv_pct(100));
    dk_row(bottom, 8);
    lv_obj_set_flex_align(bottom, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *pill = dk_pill(bottom, dk_state_pill(a->state), DK_FONT_XS, dk_state_label(a->state));
    if (a->state == DK_NEEDS_YOU || a->state == DK_WAITING) dk_anim_wiggle(pill);
    if (a->state == DK_WORKING) ui_eq_bars(bottom, 3, 22, DK_PURPLE);
    char ago[12];
    ui_ago(a->updated, ago, sizeof(ago));
    dk_label(bottom, DK_FONT_XS, DK_INK_SOFT, ago);

    if (animate) dk_anim_rise(c, index * 60);
}

void scr_sessions_refresh(void)
{
    if (!s.scr) return;
    for (int i = 0; i < 4; i++) {
        bool on = (filter_t)i == s.filter;
        lv_obj_set_style_bg_opa(s.tabs[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s.tabs[i], 0), on ? DK_GROUND : DK_INK, 0);
    }
    ui_nav_rail_update(s.rail);

    // "6 running on 3 machines · 2 need you"
    char hosts[DK_MAX_AGENTS][48];
    int host_n = 0;
    for (int i = 0; i < g_model->agent_n; i++) {
        bool seen = false;
        for (int j = 0; j < host_n; j++) seen |= strcmp(hosts[j], g_model->agents[i].host) == 0;
        if (!seen) snprintf(hosts[host_n++], sizeof(hosts[0]), "%s", g_model->agents[i].host);
    }
    int need = ui_count_state(DK_NEEDS_YOU) + ui_count_state(DK_WAITING);
    char sub[96];
    if (g_model->agent_n == 0) snprintf(sub, sizeof(sub), "Nothing running right now");
    else if (need) snprintf(sub, sizeof(sub), "%d running on %d machine%s · %d need%s you", g_model->agent_n, host_n, host_n == 1 ? "" : "s", need, need == 1 ? "s" : "");
    else snprintf(sub, sizeof(sub), "%d running on %d machine%s", g_model->agent_n, host_n, host_n == 1 ? "" : "s");
    dk_set_text(s.subtitle, sub);

    static uint32_t sig;
    uint32_t now = dk_hash(dk_hash(DK_HASH0, &s.filter, sizeof(s.filter)), &g_model->agent_n, sizeof(g_model->agent_n));
    now = dk_hash(now, g_model->agents, g_model->agent_n * sizeof(g_model->agents[0]));
    for (int i = 0; i < g_model->agent_n; i++) {
        char ago[12];
        ui_ago(g_model->agents[i].updated, ago, sizeof(ago));
        now = dk_hash_str(now, ago);
    }
    if (now == sig) return; // the grid already shows exactly this
    sig = now;

    int shown = 0;
    for (int i = 0; i < g_model->agent_n; i++) shown += matches(&g_model->agents[i]);
    bool animate = shown != s.last_count;
    s.last_count = shown;

    lv_obj_clean(s.grid);
    int k = 0;
    for (int i = 0; i < g_model->agent_n; i++) {
        if (matches(&g_model->agents[i])) card(&g_model->agents[i], k++, animate);
    }
    if (k == 0) {
        lv_obj_t *empty = dk_card(s.grid, DK_CARD, 30);
        lv_obj_set_size(empty, lv_pct(100), 200);
        dk_col(empty, 8);
        lv_obj_set_flex_align(empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        dk_label(empty, DK_FONT_XL, DK_INK, g_model->agent_n ? "Nothing here" : "No sessions yet");
        dk_label(empty, DK_FONT_M, DK_MUTED, g_model->agent_n ? "Try another filter." : "Start Claude Code or Cursor and they'll appear.");
    }
}
