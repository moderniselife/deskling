// First-run setup: say hi, pick a Wi-Fi network, type its password, then pair with the relay.
// Pairing happens on your computer (paste the relay URL + token into http://deskling.local)
// or right here with the on-screen keyboard.
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define MAX_APS 16

typedef enum { P_WELCOME, P_WIFI, P_PASSWORD, P_JOINING, P_PAIR, P_MANUAL, P_COUNT } page_t;

static struct {
    lv_obj_t *scr, *face, *pages[P_COUNT];
    lv_obj_t *ap_list, *scan_status, *pw_title, *pw_ta, *pw_error, *join_label;
    lv_obj_t *pair_qr_slot, *pair_url, *pair_code, *pair_status, *hotspot_note;
    lv_obj_t *url_ta, *token_ta, *kb;
    page_t page;
    ui_wifi_ap_t aps[MAX_APS];
    int ap_n;
    char chosen[33];
    bool chosen_secure;
    bool scanning;
} su;

static void show_page(page_t p)
{
    su.page = p;
    for (int i = 0; i < P_COUNT; i++) {
        if (i == (int)p) lv_obj_remove_flag(su.pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(su.pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    dk_anim_rise(su.pages[p], 0);
    lv_obj_add_flag(su.kb, LV_OBJ_FLAG_HIDDEN);
    dk_mood_t mood = p == P_JOINING ? DK_MOOD_BUSY : p == P_WELCOME ? DK_MOOD_HAPPY : DK_MOOD_CURIOUS;
    if (p == P_PAIR && g_info.relay_configured) mood = DK_MOOD_HAPPY;
    deskling_set_mood(su.face, mood);
    if (p == P_PASSWORD) {
        lv_keyboard_set_textarea(su.kb, su.pw_ta);
        lv_obj_remove_flag(su.kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *page(const char *title, const char *sub)
{
    lv_obj_t *p = dk_box(su.scr);
    lv_obj_set_flex_grow(p, 1);
    lv_obj_set_height(p, lv_pct(100));
    lv_obj_set_width(p, 0);
    dk_col(p, 14);
    lv_obj_t *t = dk_label(p, DK_FONT_H1, DK_INK, title);
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
    if (sub) {
        lv_obj_t *s = dk_label(p, DK_FONT_L, DK_INK_SOFT, sub);
        lv_obj_set_width(s, lv_pct(100));
        lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_WRAP);
    }
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

static lv_obj_t *text_area(lv_obj_t *parent, const char *placeholder)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_obj_set_size(ta, lv_pct(100), 64);
    lv_obj_set_style_radius(ta, 20, 0);
    lv_obj_set_style_border_width(ta, 3, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0xE4DCCF), 0);
    lv_obj_set_style_border_color(ta, DK_PURPLE, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(ta, DK_CARD, 0);
    lv_obj_set_style_text_font(ta, DK_FONT_L, 0);
    lv_obj_set_style_text_color(ta, DK_INK, 0);
    lv_obj_set_style_pad_hor(ta, 18, 0);
    lv_obj_set_style_pad_ver(ta, 16, 0);
    return ta;
}

// ---------------------------------------------------------------- actions

static void start_scan(void)
{
    su.scanning = true;
    lv_label_set_text(su.scan_status, "Looking for networks…");
    lv_obj_clean(su.ap_list);
    if (g_platform && g_platform->wifi_scan) g_platform->wifi_scan();
}

static void go_wifi(lv_event_t *e)
{
    (void)e;
    show_page(P_WIFI);
    start_scan();
}

static void rescan(lv_event_t *e)
{
    (void)e;
    start_scan();
}

static void join(void)
{
    char label[96];
    snprintf(label, sizeof(label), "Joining %s…", su.chosen);
    lv_label_set_text(su.join_label, label);
    show_page(P_JOINING);
    if (g_platform && g_platform->wifi_connect) g_platform->wifi_connect(su.chosen, lv_textarea_get_text(su.pw_ta));
}

static void pick_ap(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    snprintf(su.chosen, sizeof(su.chosen), "%s", su.aps[i].ssid);
    su.chosen_secure = su.aps[i].secure;
    lv_textarea_set_text(su.pw_ta, "");
    lv_label_set_text(su.pw_error, "");
    if (!su.chosen_secure) {
        join();
        return;
    }
    char t[80];
    snprintf(t, sizeof(t), "Password for %s", su.chosen);
    lv_label_set_text(su.pw_title, t);
    show_page(P_PASSWORD);
}

static void pw_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) join();
    else if (code == LV_EVENT_CANCEL) show_page(P_WIFI);
}

static void join_cb(lv_event_t *e)
{
    (void)e;
    join();
}

static void back_to_wifi(lv_event_t *e)
{
    (void)e;
    show_page(P_WIFI);
}

static void show_pw_cb(lv_event_t *e)
{
    (void)e;
    lv_textarea_set_password_mode(su.pw_ta, !lv_textarea_get_password_mode(su.pw_ta));
}

static void go_manual(lv_event_t *e)
{
    (void)e;
    if (g_info.relay_url[0]) lv_textarea_set_text(su.url_ta, g_info.relay_url);
    show_page(P_MANUAL);
}

static void go_pair(lv_event_t *e)
{
    (void)e;
    show_page(P_PAIR);
}

static void manual_focus(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(su.kb, ta);
    if (lv_obj_has_flag(su.kb, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(su.kb, LV_OBJ_FLAG_HIDDEN);
        dk_anim_drop_in(su.kb, 120, 0);
    }
}

static void save_manual(lv_event_t *e)
{
    (void)e;
    const char *url = lv_textarea_get_text(su.url_ta);
    const char *token = lv_textarea_get_text(su.token_ta);
    if (!url[0] || !token[0]) return;
    if (g_platform && g_platform->relay_set) g_platform->relay_set(url, token);
    show_page(P_PAIR);
    lv_label_set_text(su.pair_status, "Connecting to your relay…");
}

static void kb_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (su.page == P_PASSWORD) {
        pw_event(e);
    } else if (su.page == P_MANUAL && (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)) {
        lv_obj_add_flag(su.kb, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---------------------------------------------------------------- build

lv_obj_t *scr_setup_create(void)
{
    su.scr = lv_obj_create(NULL);
    lv_obj_remove_flag(su.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(su.scr, DK_GROUND, 0);
    lv_obj_set_style_pad_hor(su.scr, 56, 0);
    lv_obj_set_style_pad_ver(su.scr, 44, 0);
    dk_row(su.scr, 48);
    lv_obj_set_flex_align(su.scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *left = dk_card(su.scr, DK_LILAC, 40);
    lv_obj_set_size(left, 340, lv_pct(100));
    dk_col(left, 18);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *fw = dk_box(left);
    lv_obj_set_size(fw, 240, 240);
    dk_overflow(fw, 80);
    su.face = deskling_create(fw, 220, DK_FACE);
    lv_obj_center(su.face);
    dk_label(left, DK_FONT_XL, DK_INK, "deskling");
    su.hotspot_note = dk_label(left, DK_FONT_S, DK_INK_SOFT, "");
    lv_obj_set_width(su.hotspot_note, lv_pct(100));
    lv_label_set_long_mode(su.hotspot_note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(su.hotspot_note, LV_TEXT_ALIGN_CENTER, 0);

    // Welcome
    lv_obj_t *p = su.pages[P_WELCOME] = page("Hi! I'm Deskling.", "I keep an eye on your AI agents and tell you when they need you. Let's get me online.");
    lv_obj_t *b = dk_button(p, "Choose Wi-Fi  " LV_SYMBOL_WIFI, true, DK_FONT_XL);
    lv_obj_set_height(b, 84);
    lv_obj_set_style_pad_hor(b, 36, 0);
    lv_obj_add_event_cb(b, go_wifi, LV_EVENT_CLICKED, NULL);
    dk_anim_glow(b, DK_INK);

    // Wi-Fi list
    p = su.pages[P_WIFI] = page("Pick your Wi-Fi", NULL);
    lv_obj_t *row = dk_box(p);
    lv_obj_set_width(row, lv_pct(100));
    dk_row(row, 12);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    su.scan_status = dk_label(row, DK_FONT_L, DK_INK_SOFT, "");
    lv_obj_t *rb = dk_button(row, LV_SYMBOL_REFRESH "  Scan again", false, DK_FONT_M);
    lv_obj_add_event_cb(rb, rescan, LV_EVENT_CLICKED, NULL);
    su.ap_list = dk_box(p);
    lv_obj_set_width(su.ap_list, lv_pct(100));
    lv_obj_set_flex_grow(su.ap_list, 1);
    dk_col(su.ap_list, 10);
    lv_obj_add_flag(su.ap_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(su.ap_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(su.ap_list, LV_SCROLLBAR_MODE_OFF);

    // Password
    p = su.pages[P_PASSWORD] = page("", NULL);
    su.pw_title = lv_obj_get_child(p, 0);
    su.pw_ta = text_area(p, "Wi-Fi password");
    lv_textarea_set_password_mode(su.pw_ta, true);
    lv_obj_add_state(su.pw_ta, LV_STATE_FOCUSED);
    row = dk_box(p);
    dk_row(row, 12);
    lv_obj_t *jb = dk_button(row, "Connect", true, DK_FONT_L);
    lv_obj_add_event_cb(jb, join_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sb = dk_button(row, LV_SYMBOL_EYE_OPEN "  Show", false, DK_FONT_M);
    lv_obj_add_event_cb(sb, show_pw_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bb = dk_button(row, "Other network", false, DK_FONT_M);
    lv_obj_add_event_cb(bb, back_to_wifi, LV_EVENT_CLICKED, NULL);
    su.pw_error = dk_label(p, DK_FONT_L, DK_ALERT_INK, "");

    // Joining
    p = su.pages[P_JOINING] = page("Joining…", "This takes a few seconds.");
    su.join_label = lv_obj_get_child(p, 0);
    ui_eq_bars(p, 5, 34, DK_PURPLE);

    // Pair with the relay
    p = su.pages[P_PAIR] = page("Now let's meet your relay", "On your computer, open this address and paste your relay URL and token. Or scan the code with your phone.");
    lv_obj_t *pr = dk_box(p);
    lv_obj_set_width(pr, lv_pct(100));
    dk_row(pr, 28);
    su.pair_qr_slot = dk_box(pr);
    lv_obj_t *pt = dk_box(pr);
    lv_obj_set_flex_grow(pt, 1);
    lv_obj_set_width(pt, 0);
    dk_col(pt, 10);
    su.pair_url = dk_label(pt, DK_FONT_H2, DK_PURPLE, "");
    lv_obj_set_width(su.pair_url, lv_pct(100));
    lv_label_set_long_mode(su.pair_url, LV_LABEL_LONG_MODE_WRAP);
    su.pair_code = dk_label(pt, DK_FONT_XL, DK_INK, "");
    su.pair_status = dk_label(pt, DK_FONT_M, DK_INK_SOFT, "Waiting for your computer…");
    lv_obj_set_width(su.pair_status, lv_pct(100));
    lv_label_set_long_mode(su.pair_status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_t *mb = dk_button(p, "Type it in here instead", false, DK_FONT_M);
    lv_obj_add_event_cb(mb, go_manual, LV_EVENT_CLICKED, NULL);

    // Manual relay entry
    p = su.pages[P_MANUAL] = page("Relay details", NULL);
    su.url_ta = text_area(p, "https://deskling-relay.you.workers.dev");
    lv_obj_add_event_cb(su.url_ta, manual_focus, LV_EVENT_FOCUSED, NULL);
    su.token_ta = text_area(p, "Relay token");
    lv_obj_add_event_cb(su.token_ta, manual_focus, LV_EVENT_FOCUSED, NULL);
    row = dk_box(p);
    dk_row(row, 12);
    lv_obj_t *svb = dk_button(row, "Save", true, DK_FONT_L);
    lv_obj_add_event_cb(svb, save_manual, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cb = dk_button(row, "Back", false, DK_FONT_M);
    lv_obj_add_event_cb(cb, go_pair, LV_EVENT_CLICKED, NULL);

    su.kb = lv_keyboard_create(su.scr);
    lv_obj_add_flag(su.kb, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(su.kb, DK_W, 330);
    lv_obj_align(su.kb, LV_ALIGN_BOTTOM_MID, 0, 44);
    lv_obj_set_style_bg_color(su.kb, lv_color_hex(0xEDE6DA), 0);
    lv_obj_set_style_text_font(su.kb, DK_FONT_L, LV_PART_ITEMS);
    lv_obj_set_style_radius(su.kb, 14, LV_PART_ITEMS);
    lv_obj_add_event_cb(su.kb, kb_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(su.kb, kb_event, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(su.kb, LV_OBJ_FLAG_HIDDEN);

    show_page(P_WELCOME);
    scr_setup_refresh();
    return su.scr;
}

// ---------------------------------------------------------------- updates

void scr_setup_refresh(void)
{
    if (!su.scr) return;
    char buf[200];
    if (g_info.ap_ssid[0] && !g_info.wifi_connected) {
        snprintf(buf, sizeof(buf), "Touch not working? Join the Wi-Fi \"%s\" from your phone and open http://192.168.4.1\nCode: %s", g_info.ap_ssid, g_info.code);
    } else if (g_info.ip[0]) {
        snprintf(buf, sizeof(buf), "%s\n%s", g_info.ssid, g_info.ip);
    } else {
        buf[0] = '\0';
    }
    lv_label_set_text(su.hotspot_note, buf);

    lv_label_set_text(su.pair_url, ui_portal_url());
    snprintf(buf, sizeof(buf), "Code: %s", g_info.code);
    lv_label_set_text(su.pair_code, buf);
    lv_obj_clean(su.pair_qr_slot);
    ui_qr(su.pair_qr_slot, ui_portal_url(), 200);

    // Once Wi-Fi is up, jump forward to pairing.
    if (g_info.wifi_connected && (su.page == P_WELCOME || su.page == P_JOINING) && !g_info.relay_configured) show_page(P_PAIR);
    if (g_info.relay_configured) lv_label_set_text(su.pair_status, "Got it! Connecting…");
}

void scr_setup_wifi(void)
{
    show_page(P_WIFI);
    start_scan();
}

static void ap_row(int i)
{
    const ui_wifi_ap_t *ap = &su.aps[i];
    lv_obj_t *row = lv_button_create(su.ap_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 72);
    lv_obj_set_style_bg_color(row, DK_CARD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 22, 0);
    lv_obj_set_style_pad_hor(row, 22, 0);
    dk_row(row, 16);
    lv_obj_add_event_cb(row, pick_ap, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    dk_press_style(row);
    // Signal as 1-4 bars.
    int bars = ap->rssi > -55 ? 4 : ap->rssi > -65 ? 3 : ap->rssi > -75 ? 2 : 1;
    lv_obj_t *sig = dk_box(row);
    lv_obj_set_height(sig, 26);
    lv_obj_set_flex_flow(sig, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sig, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(sig, 3, 0);
    for (int k = 0; k < 4; k++) {
        lv_obj_t *bar = dk_box(sig);
        lv_obj_set_size(bar, 6, 8 + k * 6);
        lv_obj_set_style_radius(bar, 3, 0);
        lv_obj_set_style_bg_color(bar, k < bars ? DK_INK : lv_color_hex(0xDDD5C8), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    }
    lv_obj_t *n = dk_label(row, DK_FONT_L, DK_INK, ap->ssid);
    lv_obj_set_flex_grow(n, 1);
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    if (ap->secure) {
        // A little padlock: shackle arc + body.
        lv_obj_t *lock = dk_box(row);
        lv_obj_set_size(lock, 20, 24);
        lv_obj_t *shackle = lv_arc_create(lock);
        lv_obj_remove_style_all(shackle);
        lv_obj_remove_flag(shackle, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(shackle, 14, 14);
        lv_obj_align(shackle, LV_ALIGN_TOP_MID, 0, 0);
        lv_arc_set_bg_angles(shackle, 180, 360);
        lv_arc_set_angles(shackle, 180, 360);
        lv_obj_set_style_arc_width(shackle, 3, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(shackle, DK_MUTED, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(shackle, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_t *body = dk_box(lock);
        lv_obj_set_size(body, 20, 13);
        lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_radius(body, 4, 0);
        lv_obj_set_style_bg_color(body, DK_MUTED, 0);
        lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    }
    dk_anim_rise(row, i * 40);
}

void scr_setup_scan_done(const ui_wifi_ap_t *aps, int n)
{
    if (!su.scr) return;
    su.scanning = false;
    su.ap_n = n > MAX_APS ? MAX_APS : n;
    memcpy(su.aps, aps, sizeof(ui_wifi_ap_t) * su.ap_n);
    lv_obj_clean(su.ap_list);
    lv_label_set_text(su.scan_status, su.ap_n ? "Tap your network" : "No networks found. Try again?");
    for (int i = 0; i < su.ap_n; i++) ap_row(i);
}

void scr_setup_connect_result(bool ok, const char *message)
{
    if (!su.scr) return;
    if (ok) {
        deskling_celebrate(su.face, 1500);
        ui_sound(DK_SND_DONE);
        if (!g_info.relay_configured) show_page(P_PAIR);
        else if (ui_current() == SCR_SETUP) ui_go(SCR_HOME);
        return;
    }
    ui_sound(DK_SND_OOPS);
    lv_label_set_text(su.pw_error, message && message[0] ? message : "Couldn't join. Check the password?");
    if (su.chosen_secure) show_page(P_PASSWORD);
    else show_page(P_WIFI);
    lv_label_set_text(su.pw_error, message && message[0] ? message : "Couldn't join. Check the password?");
    dk_anim_shake(su.pw_ta);
}
