// Settings: Wi-Fi, relay, firmware updates, brightness, sound, restart.
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

static struct {
    lv_obj_t *scr, *rail;
    lv_obj_t *wifi_name, *wifi_detail;
    lv_obj_t *relay_url, *relay_state, *portal_url, *portal_code, *qr_slot;
    lv_obj_t *fw_version, *fw_status, *fw_bar, *fw_button;
    lv_obj_t *bright, *volume, *quiet;
    char qr_for[64];
} st;

static lv_obj_t *section(lv_obj_t *parent, const char *heading)
{
    lv_obj_t *c = dk_card(parent, DK_CARD, 30);
    lv_obj_set_style_pad_hor(c, 24, 0);
    lv_obj_set_style_pad_ver(c, 20, 0);
    dk_col(c, 8);
    dk_heading(c, heading);
    return c;
}

static void change_wifi(lv_event_t *e)
{
    (void)e;
    scr_setup_wifi();
    ui_go(SCR_SETUP);
}

static void fw_button_cb(lv_event_t *e)
{
    (void)e;
    if (!g_platform) return;
    if (g_update_state == UI_UPDATE_AVAILABLE && g_platform->start_update) g_platform->start_update();
    else if (g_platform->check_update) g_platform->check_update();
}

static void bright_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    int v = lv_slider_get_value(s);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED && g_platform && g_platform->set_brightness) g_platform->set_brightness((uint8_t)v);
}

static void sound_cb(lv_event_t *e)
{
    (void)e;
    uint8_t vol = (uint8_t)lv_slider_get_value(st.volume);
    bool quiet = lv_obj_has_state(st.quiet, LV_STATE_CHECKED);
    if (g_platform && g_platform->set_sound) g_platform->set_sound(vol, quiet);
    g_info.volume = vol;
    g_info.quiet_night = quiet;
    if (lv_event_get_target(e) == st.volume && g_platform && g_platform->play_sound && vol > 0) g_platform->play_sound(DK_SND_DONE);
}

static lv_obj_t *slider_row(lv_obj_t *parent, const char *label, int32_t lo, int32_t hi)
{
    lv_obj_t *row = dk_box(parent);
    lv_obj_set_size(row, lv_pct(100), 48);
    dk_row(row, 18);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *l = dk_label(row, DK_FONT_S, DK_INK_SOFT, label);
    lv_obj_set_width(l, 110);
    lv_obj_t *sl = lv_slider_create(row);
    lv_obj_set_flex_grow(sl, 1);
    lv_obj_set_height(sl, 12);
    lv_slider_set_range(sl, lo, hi);
    lv_obj_set_style_radius(sl, 6, 0);
    lv_obj_set_style_radius(sl, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sl, DK_LINE, 0);
    lv_obj_set_style_bg_color(sl, DK_PURPLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, DK_INK, LV_PART_KNOB);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 9, LV_PART_KNOB);
    lv_obj_add_flag(sl, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_set_ext_click_area(sl, 16);
    return sl;
}

static void restart_cb(lv_event_t *e)
{
    (void)e;
    if (g_platform && g_platform->restart) g_platform->restart();
}

lv_obj_t *scr_settings_create(void)
{
    st.scr = lv_obj_create(NULL);
    lv_obj_remove_flag(st.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(st.scr, DK_GROUND, 0);
    lv_obj_set_style_pad_top(st.scr, 28, 0);
    lv_obj_set_style_pad_bottom(st.scr, 28, 0);
    lv_obj_set_style_pad_left(st.scr, 0, 0);
    lv_obj_set_style_pad_right(st.scr, 32, 0);
    dk_row(st.scr, 24);
    lv_obj_set_flex_align(st.scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    st.rail = ui_nav_rail(st.scr, SCR_SETTINGS);

    lv_obj_t *main = dk_box(st.scr);
    lv_obj_set_height(main, lv_pct(100));
    lv_obj_set_flex_grow(main, 1);
    dk_col(main, 18);
    dk_label(main, DK_FONT_H2, DK_INK, "Settings");

    lv_obj_t *cols = dk_box(main);
    lv_obj_set_width(cols, lv_pct(100));
    lv_obj_set_flex_grow(cols, 1);
    dk_row(cols, 18);
    lv_obj_set_flex_align(cols, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *c1 = dk_box(cols);
    lv_obj_set_flex_grow(c1, 1);
    lv_obj_set_width(c1, 0);
    lv_obj_set_height(c1, lv_pct(100));
    dk_col(c1, 18);
    lv_obj_t *c2 = dk_box(cols);
    lv_obj_set_flex_grow(c2, 1);
    lv_obj_set_width(c2, 0);
    lv_obj_set_height(c2, lv_pct(100));
    dk_col(c2, 18);

    // Wi-Fi
    lv_obj_t *w = section(c1, "WI-FI");
    lv_obj_set_width(w, lv_pct(100));
    st.wifi_name = dk_label(w, DK_FONT_XL, DK_INK, "");
    st.wifi_detail = dk_label(w, DK_FONT_S, DK_MUTED, "");
    lv_obj_t *wb = dk_button(w, LV_SYMBOL_WIFI "  Change Wi-Fi", false, DK_FONT_M);
    lv_obj_add_event_cb(wb, change_wifi, LV_EVENT_CLICKED, NULL);

    // Relay + web settings
    lv_obj_t *r = section(c1, "RELAY");
    lv_obj_set_width(r, lv_pct(100));
    lv_obj_set_flex_grow(r, 1);
    st.relay_url = dk_label(r, DK_FONT_M, DK_INK, "");
    lv_obj_set_width(st.relay_url, lv_pct(100));
    lv_label_set_long_mode(st.relay_url, LV_LABEL_LONG_MODE_DOTS);
    st.relay_state = dk_label(r, DK_FONT_S, DK_MUTED, "");
    lv_obj_t *pr = dk_box(r);
    lv_obj_set_width(pr, lv_pct(100));
    dk_row(pr, 16);
    lv_obj_set_style_pad_top(pr, 6, 0);
    st.qr_slot = dk_box(pr);
    lv_obj_t *pt = dk_box(pr);
    dk_col(pt, 4);
    lv_obj_set_flex_grow(pt, 1);
    lv_obj_set_width(pt, 0);
    lv_obj_t *hint = dk_label(pt, DK_FONT_S, DK_INK_SOFT, "Change the relay, token or time zone from your computer:");
    lv_obj_set_width(hint, lv_pct(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    st.portal_url = dk_label(pt, DK_FONT_L, DK_PURPLE, "");
    st.portal_code = dk_label(pt, DK_FONT_L, DK_INK, "");

    // Firmware
    lv_obj_t *f = section(c2, "FIRMWARE");
    lv_obj_set_width(f, lv_pct(100));
    st.fw_version = dk_label(f, DK_FONT_XL, DK_INK, "");
    st.fw_status = dk_label(f, DK_FONT_S, DK_MUTED, "");
    lv_obj_set_width(st.fw_status, lv_pct(100));
    lv_label_set_long_mode(st.fw_status, LV_LABEL_LONG_MODE_WRAP);
    st.fw_bar = lv_bar_create(f);
    lv_obj_set_size(st.fw_bar, lv_pct(100), 14);
    lv_obj_set_style_radius(st.fw_bar, 7, 0);
    lv_obj_set_style_radius(st.fw_bar, 7, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(st.fw_bar, DK_LINE, 0);
    lv_obj_set_style_bg_color(st.fw_bar, DK_PURPLE, LV_PART_INDICATOR);
    st.fw_button = dk_button(f, "Check for updates", true, DK_FONT_M);
    lv_obj_add_event_cb(st.fw_button, fw_button_cb, LV_EVENT_CLICKED, NULL);

    // Screen
    lv_obj_t *s = section(c2, "BRIGHTNESS & SOUND");
    lv_obj_set_width(s, lv_pct(100));
    st.bright = slider_row(s, "Brightness", 10, 100);
    lv_obj_add_event_cb(st.bright, bright_cb, LV_EVENT_RELEASED, NULL);
    st.volume = slider_row(s, "Volume", 0, 100);
    lv_obj_add_event_cb(st.volume, sound_cb, LV_EVENT_RELEASED, NULL);

    lv_obj_t *qrow = dk_box(s);
    lv_obj_set_size(qrow, lv_pct(100), LV_SIZE_CONTENT);
    dk_row(qrow, 12);
    lv_obj_set_flex_align(qrow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *ql = dk_label(qrow, DK_FONT_S, DK_INK_SOFT, "Quiet at night (10pm-7am), except reminders");
    lv_obj_set_flex_grow(ql, 1);
    lv_label_set_long_mode(ql, LV_LABEL_LONG_MODE_WRAP);
    st.quiet = lv_switch_create(qrow);
    lv_obj_set_size(st.quiet, 64, 34);
    lv_obj_set_style_bg_color(st.quiet, DK_LINE, 0);
    lv_obj_set_style_bg_color(st.quiet, DK_PURPLE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(st.quiet, lv_color_white(), LV_PART_KNOB);
    lv_obj_add_event_cb(st.quiet, sound_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *rs = dk_button(c2, LV_SYMBOL_POWER "  Restart Deskling", false, DK_FONT_M);
    lv_obj_add_event_cb(rs, restart_cb, LV_EVENT_CLICKED, NULL);

    scr_settings_refresh();
    return st.scr;
}

void scr_settings_refresh(void)
{
    if (!st.scr) return;
    ui_nav_rail_update(st.rail);
    char buf[160];

    lv_label_set_text(st.wifi_name, g_info.ssid[0] ? g_info.ssid : "Not connected");
    if (g_info.wifi_connected) snprintf(buf, sizeof(buf), "Connected · %s", g_info.ip);
    else snprintf(buf, sizeof(buf), g_info.wifi_configured ? "Trying to connect…" : "Not set up yet");
    lv_label_set_text(st.wifi_detail, buf);

    lv_label_set_text(st.relay_url, g_info.relay_url[0] ? g_info.relay_url : "No relay set yet");
    lv_label_set_text(st.relay_state, g_conn == DK_CONN_ONLINE ? "Connected" : g_info.relay_configured ? "Not connected right now" : "Set it up from your computer");
    lv_label_set_text(st.portal_url, ui_portal_url());
    snprintf(buf, sizeof(buf), "Code: %s", g_info.code);
    lv_label_set_text(st.portal_code, buf);
    if (strcmp(st.qr_for, ui_portal_url()) != 0) {
        snprintf(st.qr_for, sizeof(st.qr_for), "%s", ui_portal_url());
        lv_obj_clean(st.qr_slot);
        ui_qr(st.qr_slot, st.qr_for, 120);
    }

    snprintf(buf, sizeof(buf), "Version %s", g_info.version[0] ? g_info.version : "dev");
    lv_label_set_text(st.fw_version, buf);
    const char *status = "";
    const char *btn = "Check for updates";
    char sbuf[160];
    switch (g_update_state) {
    case UI_UPDATE_CHECKING: status = "Checking…"; break;
    case UI_UPDATE_LATEST: status = "You're on the latest version."; break;
    case UI_UPDATE_AVAILABLE:
        snprintf(sbuf, sizeof(sbuf), "Version %s is available.", g_update_latest);
        status = sbuf;
        btn = "Update now";
        break;
    case UI_UPDATE_DOWNLOADING:
        snprintf(sbuf, sizeof(sbuf), "Downloading %s… %d%%. Keep me plugged in.", g_update_latest, g_update_progress);
        status = sbuf;
        btn = "Updating…";
        break;
    case UI_UPDATE_FAILED: status = g_update_message[0] ? g_update_message : "That didn't work. Try again?"; break;
    case UI_UPDATE_DONE: status = "Restarting into the new version…"; break;
    default:
        snprintf(sbuf, sizeof(sbuf), "Build %s", g_info.variant[0] ? g_info.variant : "local");
        status = sbuf;
        break;
    }
    lv_label_set_text(st.fw_status, status);
    lv_label_set_text(lv_obj_get_child(st.fw_button, 0), btn);
    if (g_update_state == UI_UPDATE_DOWNLOADING || g_update_state == UI_UPDATE_DONE) {
        lv_obj_remove_flag(st.fw_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(st.fw_bar, g_update_progress, LV_ANIM_ON);
        lv_obj_add_state(st.fw_button, LV_STATE_DISABLED);
    } else {
        lv_obj_add_flag(st.fw_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(st.fw_button, LV_STATE_DISABLED);
    }
    if (!lv_slider_is_dragged(st.bright)) lv_slider_set_value(st.bright, g_info.brightness ? g_info.brightness : 90, LV_ANIM_OFF);
    if (!lv_slider_is_dragged(st.volume)) lv_slider_set_value(st.volume, g_info.volume, LV_ANIM_OFF);
    if (g_info.quiet_night) lv_obj_add_state(st.quiet, LV_STATE_CHECKED);
    else lv_obj_remove_state(st.quiet, LV_STATE_CHECKED);
}
