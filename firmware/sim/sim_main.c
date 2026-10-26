// Deskling UI simulator: drives the real UI code with fixture snapshots, taps buttons by
// their label, and writes screenshots (and animation frames) as PNGs.
//   deskling_sim <fixtures dir> <out dir> [--frames]
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "cJSON.h"
#include "lvgl.h"
#include "model.h"
#include "ui.h"
#include "ui_internal.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define W 1280
#define H 800

static uint32_t s_fb[W * H];
static uint32_t s_tick;
static const char *s_fixtures, *s_out;
static int s_failures;
static char s_last_sent[2048];

static lv_indev_state_t s_ptr_state = LV_INDEV_STATE_RELEASED;
static lv_point_t s_ptr;

static uint32_t tick_cb(void)
{
    return s_tick;
}

static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px)
{
    (void)area;
    (void)px;
    lv_display_flush_ready(d);
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point = s_ptr;
    data->state = s_ptr_state;
}

static void sender(const char *json)
{
    snprintf(s_last_sent, sizeof(s_last_sent), "%s", json);
    printf("  -> sent %s\n", json);
}

static void device_tick(void);
static void run(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5) {
        s_tick += 5;
        device_tick();
        lv_timer_handler();
    }
}

static void shot(const char *name)
{
    lv_refr_now(NULL);
    static uint8_t rgb[W * H * 3];
    for (int i = 0; i < W * H; i++) {
        uint32_t p = s_fb[i];
        rgb[i * 3 + 0] = (p >> 16) & 0xFF;
        rgb[i * 3 + 1] = (p >> 8) & 0xFF;
        rgb[i * 3 + 2] = p & 0xFF;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", s_out, name);
    stbi_write_png(path, W, H, 3, rgb, W * 3);
    printf("  shot %s\n", path);
}

static void frames(const char *name, uint32_t total_ms, uint32_t step_ms)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/frames_%s", s_out, name);
    mkdir(dir, 0755);
    for (uint32_t t = 0, i = 0; t < total_ms; t += step_ms, i++) {
        char n[600];
        snprintf(n, sizeof(n), "frames_%s/%03u", name, i);
        shot(n);
        run(step_ms);
    }
}

// Fixture strings like "@-120" mean "now - 120 s" in ms.
static void relativise(cJSON *node, int64_t now)
{
    for (cJSON *c = node->child; c; c = c->next) {
        if (cJSON_IsString(c) && c->valuestring && c->valuestring[0] == '@') {
            double v = (double)(now + (int64_t)atoll(c->valuestring + 1) * 1000);
            cJSON *num = cJSON_CreateNumber(v);
            if (node->type == cJSON_Array) {
                int idx = 0;
                for (cJSON *x = node->child; x && x != c; x = x->next) idx++;
                cJSON_ReplaceItemInArray(node, idx, num);
            } else {
                cJSON_ReplaceItemInObject(node, c->string, num);
            }
            c = num;
        } else if (c->child) {
            relativise(c, now);
        }
    }
}

static void apply(const char *fixture)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.json", s_fixtures, fixture);
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("missing fixture %s\n", path);
        s_failures++;
        return;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1);
    fread(buf, 1, n, f);
    buf[n] = 0;
    fclose(f);
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    relativise(root, (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    static dk_model_t m;
    if (!dk_model_parse(json, strlen(json), &m)) {
        printf("could not parse %s\n", fixture);
        s_failures++;
    }
    free(json);
    printf("apply %s\n", fixture);
    ui_apply_model(&m);
}

// Find a visible label whose text starts with `text`, on the active screen or the top layer.
static lv_obj_t *find_label(lv_obj_t *o, const char *text)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_check_type(o, &lv_label_class)) {
        const char *t = lv_label_get_text(o);
        if (t && strncmp(t, text, strlen(text)) == 0) return o;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *r = find_label(lv_obj_get_child(o, i), text);
        if (r) return r;
    }
    return NULL;
}

static bool tap(const char *text)
{
    lv_obj_t *l = find_label(lv_screen_active(), text);
    if (!l) {
        printf("FAIL  no \"%s\" on screen\n", text);
        s_failures++;
        return false;
    }
    lv_obj_t *target = l;
    while (target && !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) target = lv_obj_get_parent(target);
    if (!target) target = l;
    lv_area_t a;
    lv_obj_get_coords(target, &a);
    s_ptr.x = (a.x1 + a.x2) / 2;
    s_ptr.y = (a.y1 + a.y2) / 2;
    printf("tap \"%s\" at %d,%d\n", text, (int)s_ptr.x, (int)s_ptr.y);
    s_ptr_state = LV_INDEV_STATE_PRESSED;
    run(80);
    s_ptr_state = LV_INDEV_STATE_RELEASED;
    run(60);
    return true;
}

/** Press at (x0,y0), move to (x1,y1) over ms, optionally hold there, release. */
static void drag(int x0, int y0, int x1, int y1, uint32_t ms, uint32_t hold_ms)
{
    printf("drag %d,%d -> %d,%d%s\n", x0, y0, x1, y1, hold_ms ? " (hold)" : "");
    s_ptr.x = x0;
    s_ptr.y = y0;
    s_ptr_state = LV_INDEV_STATE_PRESSED;
    run(40);
    int steps = ms / 20 ? ms / 20 : 1;
    for (int i = 1; i <= steps; i++) {
        s_ptr.x = x0 + (x1 - x0) * i / steps;
        s_ptr.y = y0 + (y1 - y0) * i / steps;
        run(20);
    }
    if (hold_ms) run(hold_ms);
    s_ptr_state = LV_INDEV_STATE_RELEASED;
    run(80);
}

static void expect_screen(scr_id_t id, const char *name)
{
    bool ok = ui_current() == id;
    printf("%s  on the %s screen\n", ok ? "PASS" : "FAIL", name);
    if (!ok) s_failures++;
}

static void check(bool ok, const char *what)
{
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) s_failures++;
}

static lv_obj_t *find_class(lv_obj_t *o, const lv_obj_class_t *cls)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_check_type(o, cls)) return o;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *r = find_class(lv_obj_get_child(o, i), cls);
        if (r) return r;
    }
    return NULL;
}

static void tap_obj(lv_obj_t *o, const char *what)
{
    if (!o) {
        printf("FAIL  no %s on screen\n", what);
        s_failures++;
        return;
    }
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    s_ptr.x = (a.x1 + a.x2) / 2;
    s_ptr.y = (a.y1 + a.y2) / 2;
    printf("tap %s at %d,%d\n", what, (int)s_ptr.x, (int)s_ptr.y);
    s_ptr_state = LV_INDEV_STATE_PRESSED;
    run(80);
    s_ptr_state = LV_INDEV_STATE_RELEASED;
    run(60);
}

static void expect_sent(const char *needle)
{
    bool ok = strstr(s_last_sent, needle) != NULL;
    printf("%s  sent contains %s\n", ok ? "PASS" : "FAIL", needle);
    if (!ok) s_failures++;
    s_last_sent[0] = 0;
}

static void expect_text(const char *text)
{
    bool ok = find_label(lv_screen_active(), text) || find_label(lv_layer_top(), text);
    printf("%s  screen shows \"%s\"\n", ok ? "PASS" : "FAIL", text);
    if (!ok) s_failures++;
}

// ------------------------------------------------------- fake device
static ui_device_info_t s_info;
static int s_pending_scan, s_pending_connect, s_pending_update;

static void fake_scan(void) { s_pending_scan = 1; }
static void fake_connect(const char *ssid, const char *pw)
{
    printf("  -> wifi_connect %s / %s\n", ssid, pw);
    snprintf(s_info.ssid, sizeof(s_info.ssid), "%s", ssid);
    s_pending_connect = 1;
}
static void fake_relay_set(const char *url, const char *token)
{
    printf("  -> relay_set %s %s\n", url, token);
    snprintf(s_info.relay_url, sizeof(s_info.relay_url), "%s", url);
    s_info.relay_configured = true;
    ui_set_device_info(&s_info);
}
static void fake_check(void) { ui_update_status(UI_UPDATE_CHECKING, 0, NULL, NULL); s_pending_update = 1; }
static void fake_start(void) { ui_update_status(UI_UPDATE_DOWNLOADING, 0, "v0.3.0", NULL); s_pending_update = 2; }
static void fake_bright(uint8_t v) { printf("  -> brightness %u\n", v); }
static void fake_restart(void) { printf("  -> restart\n"); }
static char s_last_sound[24];
static void fake_sound(dk_sound_t snd)
{
    if (snd == DK_SND_TAP) return; // every press; too noisy to log
    snprintf(s_last_sound, sizeof(s_last_sound), "%s", sound_name(snd));
    printf("  -> sound %s\n", s_last_sound);
}
static void fake_set_sound(uint8_t vol, bool quiet) { printf("  -> sound settings %u quiet=%d\n", vol, quiet); }
static const ui_platform_t s_fake = {fake_scan, fake_connect, fake_relay_set, fake_check, fake_start, fake_bright, fake_restart, fake_sound, fake_set_sound};

static void expect_sound(const char *name)
{
    bool ok = strcmp(s_last_sound, name) == 0;
    printf("%s  plays the %s sound%s%s\n", ok ? "PASS" : "FAIL", name, ok ? "" : ", got ", ok ? "" : s_last_sound);
    if (!ok) s_failures++;
    s_last_sound[0] = 0;
}

// Deliver the fake device's async results after a little delay, like the real one.
static void device_tick(void)
{
    if (s_pending_scan && ++s_pending_scan > 60) {
        static const ui_wifi_ap_t aps[] = {{"HomeNet", -48, true}, {"HomeNet-5G", -61, true}, {"Neighbours Wi-Fi", -79, true}, {"Cafe Guest", -84, false}};
        ui_wifi_scan_done(aps, 4);
        s_pending_scan = 0;
    }
    if (s_pending_connect && ++s_pending_connect > 120) {
        s_pending_connect = 0;
        s_info.wifi_configured = s_info.wifi_connected = true;
        snprintf(s_info.ip, sizeof(s_info.ip), "192.168.1.42");
        snprintf(s_info.hostname, sizeof(s_info.hostname), "deskling.local");
        s_info.ap_ssid[0] = 0;
        ui_set_device_info(&s_info);
        ui_wifi_connect_result(true, NULL);
    }
    if (s_pending_update == 1 && ++s_pending_update > 1) {
        ui_update_status(UI_UPDATE_AVAILABLE, 0, "v0.3.0", NULL);
        s_pending_update = 0;
    }
    if (s_pending_update >= 2) {
        int pct = (s_pending_update - 2) / 2;
        if (pct <= 100) ui_update_status(UI_UPDATE_DOWNLOADING, pct, "v0.3.0", NULL), s_pending_update++;
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <fixtures dir> <out dir> [--frames]\n", argv[0]);
        return 2;
    }
    s_fixtures = argv[1];
    s_out = argv[2];
    bool want_frames = argc > 3 && strcmp(argv[3], "--frames") == 0;
    mkdir(s_out, 0755);

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(disp, s_fb, NULL, sizeof(s_fb), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_indev_t *ptr = lv_indev_create();
    lv_indev_set_type(ptr, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(ptr, read_cb);

    dk_set_sender(sender);
    ui_init();
    ui_set_platform(&s_fake);

    // ---- first-run setup
    snprintf(s_info.code, sizeof(s_info.code), "4821");
    snprintf(s_info.ap_ssid, sizeof(s_info.ap_ssid), "Deskling-7F3A");
    snprintf(s_info.version, sizeof(s_info.version), "v0.2.0");
    snprintf(s_info.variant, sizeof(s_info.variant), "p4r1-lcd1");
    s_info.brightness = 90;
    s_info.volume = 60;
    s_info.quiet_night = false;
    ui_set_device_info(&s_info);
    ui_set_connection(DK_CONN_SETUP, NULL);
    run(1200);
    shot("00a_setup_welcome");
    expect_text("Hi! I'm Deskling.");
    tap("Choose Wi-Fi");
    run(800);
    shot("00b_setup_wifi_list");
    expect_text("HomeNet");
    tap("HomeNet");
    run(600);
    shot("00c_setup_password");
    expect_text("Password for HomeNet");
    lv_textarea_set_text(find_class(lv_screen_active(), &lv_textarea_class), "hunter22");
    tap("Connect");
    run(300);
    shot("00d_setup_joining");
    run(1200);
    shot("00e_setup_pair");
    expect_text("Now let's meet your relay");
    expect_text("Code: 4821");
    fake_relay_set("https://deskling-relay.joe.workers.dev", "secret");
    run(900);
    shot("00f_setup_done");
    expect_text("All set!");
    expect_sound("hello");
    ui_set_connection(DK_CONN_RELAY, NULL);
    run(5000);
    shot("01_home_connecting");

    apply("empty");
    ui_set_connection(DK_CONN_ONLINE, NULL);
    run(1000);
    shot("02_home_empty");

    apply("busy");
    run(1500);
    shot("03_home_busy");
    expect_text("Claude needs you");
    if (want_frames) frames("home", 3000, 100);

    apply("done");
    run(200);
    expect_sound("done");
    if (want_frames) frames("home_done", 2000, 100);
    run(900);
    shot("04_home_done_cheer");

    tap("Review the PR");
    run(200);
    tap("Stand up");
    run(600);
    expect_sent("\"type\":\"reminder_done\"");
    shot("05_home_reminder_ticked");

    apply("busy");
    run(2500);
    tap("See all");
    run(900);
    shot("06_sessions");
    expect_text("Sessions");

    tap("web-app");
    run(900);
    shot("07_chat");
    tap("Run the tests");
    run(400);
    expect_sent("\"text\":\"Run the tests\"");
    tap_obj(find_class(lv_screen_active(), &lv_textarea_class), "the message box");
    run(800);
    {
        bool kb = find_class(lv_screen_active(), &lv_keyboard_class) != NULL;
        printf("%s  keyboard opens\n", kb ? "PASS" : "FAIL");
        if (!kb) s_failures++;
    }

    shot("08_chat_keyboard");

    apply("permission");
    run(1200);
    shot("09_permission");
    expect_text("Claude wants to run a command");
    expect_sound("permission");
    if (want_frames) frames("permission", 2500, 100);
    tap("Allow once");
    run(500);
    expect_sent("\"decision\":\"allow\"");
    expect_sound("allow");
    shot("10_permission_allowed");
    apply("busy");
    run(2500);

    apply("question");
    run(1200);
    shot("11_question");
    expect_text("Which database");
    expect_sound("question");
    if (want_frames) frames("question", 2000, 100);
    tap("SQLite");
    run(500);
    expect_sent("\"Which database should the cache use?\":\"SQLite\"");
    shot("12_question_answered");
    apply("busy");
    run(2500);

    apply("reminder_due");
    run(900);
    shot("13_reminder_banner");
    expect_text("Reminder");
    expect_sound("reminder");

    run(5000);
    tap("Sessions");
    run(800);
    tap("Settings");
    run(800);
    shot("16_settings");
    expect_text("Settings");
    expect_text("Quiet at night (10pm-7am), except reminders");
    tap("Check for updates");
    run(300);
    expect_text("Version v0.3.0 is available.");
    tap("Update now");
    run(500);
    shot("17_settings_updating");
    tap("Night");
    run(1500);
    shot("14_night");
    if (want_frames) frames("night", 3000, 100);
    tap("deskling");
    run(800);
    shot("15_back_home");

    // ---- gestures
    expect_screen(SCR_HOME, "home");
    drag(640, 8, 640, 420, 300, 0);
    run(500);
    check(ui_gestures_panel_open(), "swipe down from the top opens notifications");
    check(find_label(lv_layer_top(), "Notifications") != NULL, "notification panel has its heading");
    shot("18_notifications");
    drag(640, 420, 640, 120, 200, 0);
    run(400);
    check(!ui_gestures_panel_open(), "swipe up closes notifications");
    drag(640, 795, 640, 560, 250, 700);
    run(500);
    expect_screen(SCR_SESSIONS, "sessions (swipe up and hold)");
    tap("web-app");
    run(600);
    expect_screen(SCR_CHAT, "chat");
    drag(6, 420, 280, 430, 250, 0);
    run(600);
    expect_screen(SCR_SESSIONS, "sessions (swipe back from chat)");
    drag(640, 795, 640, 600, 150, 0);
    run(600);
    expect_screen(SCR_HOME, "home (swipe up)");
    tap("See all");
    run(600);
    tap("Settings");
    run(600);
    expect_screen(SCR_SETTINGS, "settings");
    {
        // A back swipe that starts on the rail's Sessions button must not press it.
        lv_obj_t *l = find_label(lv_screen_active(), "Sessions");
        lv_area_t a;
        lv_obj_get_coords(l, &a);
        drag(14, (a.y1 + a.y2) / 2 - 20, 290, (a.y1 + a.y2) / 2 - 20, 250, 0);
        run(600);
        expect_screen(SCR_HOME, "home (back swipe started on a rail button)");
    }

    printf(s_failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", s_failures);
    return s_failures ? 1 : 0;
}
