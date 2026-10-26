// Start a new session from the screen: pick where (the cloud, or a computer running the
// Deskling runner) and which project, then say what you want. It starts listening for you.
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

static struct {
    lv_obj_t *scr, *face, *where, *project_box, *projects, *ta, *kb, *start, *hint;
    char target[48];
    char project[48];
    uint32_t sig;
} n;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_go(SCR_SESSIONS);
}

static const dk_target_t *current_target(void)
{
    for (int i = 0; i < g_model->targets_n; i++)
        if (strcmp(g_model->targets[i].id, n.target) == 0) return &g_model->targets[i];
    return NULL;
}

static void build(void);

static void target_cb(lv_event_t *e)
{
    const dk_target_t *t = &g_model->targets[(intptr_t)lv_event_get_user_data(e)];
    snprintf(n.target, sizeof(n.target), "%s", t->id);
    snprintf(n.project, sizeof(n.project), "%s", t->projects_n ? t->projects[0] : "");
    n.sig = 0;
    build();
}

static void project_cb(lv_event_t *e)
{
    const dk_target_t *t = current_target();
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (!t || i >= t->projects_n) return;
    snprintf(n.project, sizeof(n.project), "%s", t->projects[i]);
    n.sig = 0;
    build();
}

static void kb_hide(void)
{
    lv_obj_add_flag(n.kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_state(n.ta, LV_STATE_FOCUSED);
}

static void start_cb(lv_event_t *e)
{
    (void)e;
    const char *text = lv_textarea_get_text(n.ta);
    if (!n.target[0] || !text || !text[0]) return;
    dk_send_start(n.target, n.project, text);
    lv_textarea_set_text(n.ta, "");
    kb_hide();
    deskling_celebrate(n.face, 900);
    ui_go(SCR_SESSIONS);
}

static void ta_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        if (lv_obj_has_flag(n.kb, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_remove_flag(n.kb, LV_OBJ_FLAG_HIDDEN);
            dk_anim_drop_in(n.kb, 120, 0);
        }
    } else if (code == LV_EVENT_READY) {
        start_cb(e);
    } else if (code == LV_EVENT_CANCEL) {
        kb_hide();
    }
}

static lv_obj_t *chip(lv_obj_t *parent, const char *text, bool on)
{
    lv_obj_t *b = dk_button(parent, text, on, DK_FONT_S);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_height(b, 54);
    return b;
}

/* Where and which project: rebuilt when the choices or what's on offer change. */
static void build(void)
{
    uint32_t sig = dk_hash_str(DK_HASH0, n.target);
    sig = dk_hash_str(sig, n.project);
    sig = dk_hash(sig, g_model->targets, sizeof(g_model->targets[0]) * g_model->targets_n);
    sig = dk_hash(sig, &g_model->targets_n, sizeof(g_model->targets_n));
    if (sig == n.sig) return;
    n.sig = sig;

    if (!current_target() && g_model->targets_n) {
        snprintf(n.target, sizeof(n.target), "%s", g_model->targets[0].id);
        snprintf(n.project, sizeof(n.project), "%s", g_model->targets[0].projects_n ? g_model->targets[0].projects[0] : "");
    }
    lv_obj_clean(n.where);
    for (int i = 0; i < g_model->targets_n; i++) {
        const dk_target_t *t = &g_model->targets[i];
        lv_obj_t *b = chip(n.where, t->name, strcmp(t->id, n.target) == 0);
        lv_obj_add_event_cb(b, target_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    const dk_target_t *t = current_target();
    lv_obj_clean(n.projects);
    if (t && t->projects_n) {
        lv_obj_remove_flag(n.project_box, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < t->projects_n; i++) {
            lv_obj_t *b = chip(n.projects, t->projects[i], strcmp(t->projects[i], n.project) == 0);
            lv_obj_add_event_cb(b, project_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
    } else {
        lv_obj_add_flag(n.project_box, LV_OBJ_FLAG_HIDDEN);
    }
    if (!g_model->targets_n) {
        // Nothing set up yet: say how, with the setup page one scan away.
        lv_obj_t *card = dk_card(n.where, DK_BUTTER_BG, 24);
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_style_pad_all(card, 20, 0);
        dk_row(card, 20);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *txt = dk_box(card);
        lv_obj_set_flex_grow(txt, 1);
        dk_col(txt, 8);
        dk_label(txt, DK_FONT_L, DK_INK, "Nowhere to start one yet");
        lv_obj_t *d = dk_label(txt, DK_FONT_S, DK_INK_SOFT,
                               "On a computer: run the Deskling runner (one line to copy from your relay's setup page). "
                               "It shows up here with its projects.\n\nIn the cloud: add a routine with an API trigger on the same page.");
        lv_obj_set_width(d, lv_pct(100));
        lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_WRAP);
        if (g_info.relay_url[0]) {
            char url[180];
            size_t l = strlen(g_info.relay_url);
            snprintf(url, sizeof(url), "%.*s/setup", (int)(l && g_info.relay_url[l - 1] == '/' ? l - 1 : l), g_info.relay_url);
            ui_qr(card, url, 150);
        }
        lv_label_set_text(n.hint, "Scan the code to open your relay's setup page.");
        lv_obj_add_state(n.start, LV_STATE_DISABLED);
    } else {
        lv_label_set_text(n.hint, t && t->cloud ? "A new cloud session starts with this as its first message, and listens for you on Deskling."
                                                : "Claude starts in the background on that computer with this as its first message, and listens for you on Deskling.");
        lv_obj_remove_state(n.start, LV_STATE_DISABLED);
    }
}

lv_obj_t *scr_new_create(void)
{
    n.scr = lv_obj_create(NULL);
    lv_obj_remove_flag(n.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(n.scr, DK_GROUND, 0);
    lv_obj_set_style_pad_all(n.scr, 28, 0);
    dk_row(n.scr, 24);

    lv_obj_t *left = dk_card(n.scr, DK_LILAC, 36);
    lv_obj_set_size(left, 340, lv_pct(100));
    lv_obj_set_style_pad_hor(left, 26, 0);
    lv_obj_set_style_pad_ver(left, 24, 0);
    dk_col(left, 14);
    lv_obj_t *back = lv_button_create(left);
    lv_obj_remove_style_all(back);
    lv_obj_set_style_bg_color(back, DK_CARD, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(back, 20, 0);
    lv_obj_set_height(back, 52);
    dk_row(back, 8);
    dk_label(back, &lv_font_montserrat_20, DK_INK, LV_SYMBOL_LEFT);
    dk_label(back, DK_FONT_S, DK_INK, "Sessions");
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    dk_press_style(back);
    lv_obj_t *fw = dk_box(left);
    lv_obj_set_size(fw, lv_pct(100), 190);
    dk_overflow(fw, 60);
    n.face = deskling_create(fw, 170, DK_FACE);
    lv_obj_align(n.face, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *t = dk_label(left, DK_FONT_H2, DK_INK, "New session");
    lv_obj_set_width(t, lv_pct(100));
    n.hint = dk_label(left, DK_FONT_M, DK_INK_SOFT, "");
    lv_obj_set_width(n.hint, lv_pct(100));
    lv_label_set_long_mode(n.hint, LV_LABEL_LONG_MODE_WRAP);

    lv_obj_t *right = dk_card(n.scr, DK_CARD, 36);
    lv_obj_set_height(right, lv_pct(100));
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_style_pad_hor(right, 30, 0);
    lv_obj_set_style_pad_ver(right, 26, 0);
    dk_col(right, 14);

    dk_heading(right, "WHAT SHOULD CLAUDE DO?");
    lv_obj_t *row = dk_box(right);
    lv_obj_set_width(row, lv_pct(100));
    dk_row(row, 12);
    n.ta = lv_textarea_create(row);
    lv_textarea_set_one_line(n.ta, true);
    lv_textarea_set_placeholder_text(n.ta, "Fix the flaky login test");
    lv_obj_set_height(n.ta, 64);
    lv_obj_set_flex_grow(n.ta, 1);
    lv_obj_set_style_radius(n.ta, 22, 0);
    lv_obj_set_style_border_width(n.ta, 3, 0);
    lv_obj_set_style_border_color(n.ta, lv_color_hex(0xE4DCCF), 0);
    lv_obj_set_style_border_color(n.ta, DK_PURPLE, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(n.ta, DK_GROUND, 0);
    lv_obj_set_style_text_font(n.ta, DK_FONT_M, 0);
    lv_obj_set_style_text_color(n.ta, DK_INK, 0);
    lv_obj_set_style_pad_hor(n.ta, 20, 0);
    lv_obj_set_style_pad_ver(n.ta, 18, 0);
    lv_obj_add_event_cb(n.ta, ta_cb, LV_EVENT_ALL, NULL);
    n.start = dk_button(row, "Start", true, DK_FONT_M);
    lv_obj_set_height(n.start, 64);
    lv_obj_set_style_bg_opa(n.start, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_add_event_cb(n.start, start_cb, LV_EVENT_CLICKED, NULL);


    dk_heading(right, "WHERE");
    n.where = dk_box(right);
    lv_obj_set_width(n.where, lv_pct(100));
    lv_obj_set_flex_flow(n.where, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(n.where, 10, 0);
    lv_obj_set_style_pad_row(n.where, 10, 0);

    n.project_box = dk_box(right);
    lv_obj_set_width(n.project_box, lv_pct(100));
    dk_col(n.project_box, 14);
    dk_heading(n.project_box, "PROJECT");
    n.projects = dk_box(n.project_box);
    lv_obj_set_width(n.projects, lv_pct(100));
    lv_obj_set_flex_flow(n.projects, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(n.projects, 10, 0);
    lv_obj_set_style_pad_row(n.projects, 10, 0);
    lv_obj_set_style_max_height(n.projects, 200, 0);
    lv_obj_add_flag(n.projects, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(n.projects, LV_DIR_VER);

    n.kb = lv_keyboard_create(n.scr);
    lv_obj_add_flag(n.kb, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(n.kb, DK_W, 320);
    lv_obj_align(n.kb, LV_ALIGN_BOTTOM_MID, 0, 28);
    lv_keyboard_set_textarea(n.kb, n.ta);
    lv_obj_set_style_bg_color(n.kb, lv_color_hex(0xEDE6DA), 0);
    lv_obj_set_style_text_font(n.kb, DK_FONT_L, LV_PART_ITEMS);
    lv_obj_set_style_radius(n.kb, 14, LV_PART_ITEMS);
    lv_obj_add_event_cb(n.kb, ta_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(n.kb, ta_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(n.kb, LV_OBJ_FLAG_HIDDEN);
    return n.scr;
}

void scr_new_refresh(void)
{
    if (n.scr) build();
}
