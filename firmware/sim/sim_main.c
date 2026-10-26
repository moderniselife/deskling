// Deskling UI simulator: drives the real UI code with fixture snapshots, taps buttons by
// their label, and writes screenshots (and animation frames) as PNGs.
//   deskling_sim <fixtures dir> <out dir> [--frames]
#include <dirent.h>
#include <math.h>
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
static void frame_hook(void);
static double handler_ms(void);
static void run(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5) {
        s_tick += 5;
        device_tick();
        handler_ms();
        frame_hook();
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

static bool gesture_allowed_here(void)
{
    // The home-indicator pill is shown exactly where edge gestures work.
    lv_obj_t *top = lv_layer_top();
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
        lv_obj_t *c = lv_obj_get_child(top, i);
        if (lv_obj_get_width(c) == 150 && lv_obj_get_height(c) <= 10) return !lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN);
    }
    return false;
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

static void tap_at(int x, int y)
{
    s_ptr.x = x;
    s_ptr.y = y;
    s_ptr_state = LV_INDEV_STATE_PRESSED;
    run(120);
    s_ptr_state = LV_INDEV_STATE_RELEASED;
    run(200);
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
/* Pictures: a soft lilac gradient, delivered a moment later like a real download. */
static char s_img_id[24];
static void img_deliver(lv_timer_t *t)
{
    (void)t;
    static uint16_t px[300 * 200];
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 300; x++) {
            lv_color_t c = lv_color_mix(lv_color_hex(0xCFC4F2), lv_color_hex(0xF4B393), (uint8_t)(x * 255 / 300));
            px[y * 300 + x] = lv_color_to_u16(c);
        }
    ui_image_ready(s_img_id, px, 300, 200);
}
static void fake_fetch_image(const char *id)
{
    snprintf(s_img_id, sizeof(s_img_id), "%s", id);
    lv_timer_t *t = lv_timer_create(img_deliver, 200, NULL);
    lv_timer_set_repeat_count(t, 1);
}

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
// Raw touch = screen position, so a good calibration comes out as the identity map.
static bool s_calibrated;
static float s_cal[6];
static bool fake_touch_raw(int32_t *a, int32_t *b)
{
    *a = s_ptr.x;
    *b = s_ptr.y;
    return s_ptr_state == LV_INDEV_STATE_PRESSED;
}
static bool fake_touch_calibrated(void) { return s_calibrated; }
static void fake_touch_save(const float m[6])
{
    memcpy(s_cal, m, sizeof(s_cal));
    s_calibrated = true;
    printf("  -> touch calibration %.3f %.3f %.1f | %.3f %.3f %.1f\n", m[0], m[1], m[2], m[3], m[4], m[5]);
}
static const ui_platform_t s_fake = {fake_scan, fake_connect, fake_relay_set, fake_check, fake_start, fake_bright, fake_restart, fake_sound, fake_set_sound,
                                     fake_touch_raw, fake_touch_calibrated, fake_touch_save, fake_fetch_image};

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


#ifdef DK_BENCH
// ---- render benchmark ------------------------------------------------------------------
// Device-like setup (RGB565, two 1280x100 partial buffers) and per-frame timings of the main
// interactions. The host is many times faster than the ESP32-P4, so read the numbers as
// relative cost: the per-frame "px" column is what the device has to push, and ms/frame
// compared between scenarios shows which ones are heavy.
#include <time.h>

static uint16_t s_bench_buf[2][W * 100];
static double s_t0, s_frame_ms_sum, s_frame_ms_max;
static uint64_t s_px_sum, s_px_frame, s_px_max;
static int s_frames;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static int s_dump_areas;
static void bench_invalidate(lv_event_t *e)
{
    if (!s_dump_areas) return;
    lv_area_t *a = lv_event_get_param(e);
    printf("    invalidate %4d,%4d %4dx%-4d\n", (int)a->x1, (int)a->y1, (int)lv_area_get_width(a), (int)lv_area_get_height(a));
}

// The partial flushes assembled into one picture, for frame dumps (DK_FRAMES=<dir>).
static uint16_t s_fb565[W * H];
static const char *s_frames_dir;
static int s_frame_no;
static char s_scene_tag[32];

static void bench_flush(lv_display_t *d, const lv_area_t *area, uint8_t *px)
{
    int32_t w = lv_area_get_width(area);
    for (int32_t y = area->y1; y <= area->y2; y++)
        memcpy(&s_fb565[y * W + area->x1], (uint16_t *)px + (y - area->y1) * w, w * 2);
    s_px_frame += (uint64_t)lv_area_get_size(area);
    lv_display_flush_ready(d);
}

static void bench_event(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_REFR_START) {
        s_t0 = now_ms();
        s_px_frame = 0;
    } else if (c == LV_EVENT_REFR_READY) {
        if (!s_px_frame) return; // nothing was redrawn
        double ms = now_ms() - s_t0;
        s_frames++;
        s_frame_ms_sum += ms;
        if (ms > s_frame_ms_max) s_frame_ms_max = ms;
        s_px_sum += s_px_frame;
        if (s_px_frame > s_px_max) s_px_max = s_px_frame;
    }
}

static double s_cpu_ms;
static void bench_reset(void)
{
    s_cpu_ms = 0;
    s_frames = 0;
    s_frame_ms_sum = s_frame_ms_max = 0;
    s_px_sum = s_px_max = 0;
}

static void dump_frame(void)
{
    static uint8_t rgb[W * H * 3];
    for (int i = 0; i < W * H; i++) {
        uint16_t p = s_fb565[i];
        rgb[i * 3 + 0] = (uint8_t)(((p >> 11) & 0x1F) * 255 / 31);
        rgb[i * 3 + 1] = (uint8_t)(((p >> 5) & 0x3F) * 255 / 63);
        rgb[i * 3 + 2] = (uint8_t)((p & 0x1F) * 255 / 31);
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/%04d_%s.png", s_frames_dir, s_frame_no++, s_scene_tag);
    stbi_write_png(path, W, H, 3, rgb, W * 3);
}

static double s_total_ms, s_total_cpu;
static void bench_report(const char *name, uint32_t sim_ms)
{
    double avg = s_frames ? s_frame_ms_sum / s_frames : 0;
    s_total_ms += s_frame_ms_sum;
    s_total_cpu += s_cpu_ms;
    printf("%-28s %4d fr  draw %6.2f ms/fr avg %6.2f max  %5.0fk px/fr  | cpu %6.1f ms total, %5.2f ms per 16 ms\n", name, s_frames, avg,
           s_frame_ms_max, s_frames ? s_px_sum / 1000.0 / s_frames : 0, s_cpu_ms, s_cpu_ms * 16.0 / sim_ms);
}

#define SCENE(name, ms, body)                                                                                                   \
    do {                                                                                                                        \
        bench_reset();                                                                                                          \
        scene_tag(name);                                                                                                        \
        double t0_ = now_ms();                                                                                                  \
        body;                                                                                                                   \
        s_cpu_ms = now_ms() - t0_;                                                                                              \
        bench_report(name, ms);                                                                                                 \
    } while (0)

static void scene_tag(const char *name)
{
    int j = 0;
    for (int i = 0; name[i] && j < (int)sizeof(s_scene_tag) - 1; i++) {
        char ch = name[i];
        s_scene_tag[j++] = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ? ch : '_';
    }
    s_scene_tag[j] = 0;
}

static int bench(void)
{
    s_frames_dir = getenv("DK_FRAMES");
    if (s_frames_dir) mkdir(s_frames_dir, 0755);
    lv_display_t *disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_bench_buf[0], s_bench_buf[1], sizeof(s_bench_buf[0]), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, bench_flush);
    lv_display_add_event_cb(disp, bench_event, LV_EVENT_ALL, NULL);
    lv_display_add_event_cb(disp, bench_invalidate, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_indev_t *ptr = lv_indev_create();
    lv_indev_set_type(ptr, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(ptr, read_cb);

    dk_set_sender(sender);
    s_calibrated = true;
    ui_init();
    ui_set_platform(&s_fake);
    snprintf(s_info.version, sizeof(s_info.version), "v0.3.2");
    snprintf(s_info.ip, sizeof(s_info.ip), "192.168.1.42");
    snprintf(s_info.hostname, sizeof(s_info.hostname), "deskling.local");
    s_info.wifi_configured = s_info.wifi_connected = s_info.relay_configured = true;
    s_info.brightness = 90;
    ui_set_device_info(&s_info);
    ui_set_connection(DK_CONN_ONLINE, NULL);
    run(3200);
    apply("busy");
    run(1500);

    printf("\nhost timings: compare scenes and before/after, not absolute numbers\n");
    SCENE("home, idle 3 s", 3000, run(3000));
    if (getenv("DK_AREAS")) { puts("  home idle, one frame:"); s_dump_areas = 1; run(17); s_dump_areas = 0; }
    SCENE("home -> sessions (fade)", 600, (ui_go(SCR_SESSIONS), run(600)));
    SCENE("sessions, idle 2 s", 2000, run(2000));
    SCENE("sessions -> chat (slide)", 700, (ui_go(SCR_CHAT), run(700)));
    SCENE("chat -> sessions (slide)", 700, (ui_go(SCR_SESSIONS), run(700)));
    SCENE("sessions -> home (fade)", 600, (ui_go(SCR_HOME), run(600)));
    SCENE("pull down notifications", 900, (drag(640, 4, 640, 620, 300, 200), run(500)));
    SCENE("notifications open, idle", 1500, run(1500));
    if (getenv("DK_AREAS")) { puts("  notifications idle, one frame:"); s_dump_areas = 1; run(17); s_dump_areas = 0; }
    SCENE("push notifications away", 900, (drag(640, 560, 640, 40, 250, 0), run(600)));
    SCENE("home -> settings (fade)", 600, (ui_go(SCR_SETTINGS), run(600)));
    SCENE("settings -> home (fade)", 600, (ui_go(SCR_HOME), run(600)));
    SCENE("permission alert (over top)", 900, (apply("permission"), run(900)));
    SCENE("alert, idle 2 s", 2000, run(2000));
    if (getenv("DK_AREAS")) { puts("  alert idle, one frame:"); s_dump_areas = 1; run(17); s_dump_areas = 0; }
    SCENE("alert -> home (fade)", 900, (apply("busy"), run(900)));
    SCENE("question alert, idle 2 s", 2600, (apply("question"), run(600), bench_reset(), run(2000)));
    SCENE("question -> home", 900, (apply("busy"), run(900)));
    SCENE("home -> night (fade 700)", 1200, (ui_go(SCR_NIGHT), run(1200)));
    SCENE("night, idle 3 s", 3000, run(3000));
    SCENE("wake to home", 900, (ui_go(SCR_HOME), run(900)));
    SCENE("done: celebrate", 2500, (apply("done"), run(2500)));
    SCENE("reminder due", 2500, (apply("busy"), run(300), apply("reminder_due"), run(2200)));
    printf("\ntotal drawing: %.0f ms   total cpu: %.0f ms\n", s_total_ms, s_total_cpu);
    return 0;
}
// A fixed clock (10:00 UTC, advancing with the simulated tick) so frame dumps repeat exactly.
time_t __wrap_time(time_t *out)
{
    time_t v = (time_t)1793008800 + s_tick / 1000;
    if (out) *out = v;
    return v;
}
int __wrap_gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    tv->tv_sec = (time_t)1793008800 + s_tick / 1000;
    tv->tv_usec = (s_tick % 1000) * 1000;
    return 0;
}

static double handler_ms(void)
{
    double t = now_ms();
    lv_timer_handler();
    double d = now_ms() - t;
    s_cpu_ms += d;
    return d;
}

static void frame_hook(void)
{
    if (s_frames_dir && s_scene_tag[0] && s_tick % 50 == 0) dump_frame();
}
#else
static void frame_hook(void) {}
static double handler_ms(void)
{
    lv_timer_handler();
    return 0;
}
#endif

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <fixtures dir> <out dir> [--frames]\n", argv[0]);
        return 2;
    }
    s_fixtures = argv[1];
    s_out = argv[2];
#ifdef DK_BENCH
    setenv("TZ", "UTC", 1);
    tzset();
    lv_init();
    lv_tick_set_cb(tick_cb);
    return bench();
#endif
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

    // DK_REPLAY=<dir>/<name>.json: apply one real snapshot (say, saved from your relay), open every
    // session's chat and render it, then stop. For chasing crashes with -fsanitize=address.
    const char *replay = getenv("DK_REPLAY");
    if (replay) {
        snprintf(s_info.code, sizeof(s_info.code), "4821");
        snprintf(s_info.relay_url, sizeof(s_info.relay_url), "https://relay.example");
        ui_set_device_info(&s_info);
        ui_set_connection(DK_CONN_ONLINE, NULL);
        static char dir[400], name[100];
        snprintf(dir, sizeof(dir), "%s", replay);
        char *slash = strrchr(dir, '/');
        if (slash) *slash = 0;
        snprintf(name, sizeof(name), "%s", slash ? slash + 1 : replay);
        char *dot = strrchr(name, '.');
        if (dot) *dot = 0;
        s_fixtures = dir;
        run(3000);
        apply(name);
        run(1500);
        ui_go(SCR_SESSIONS);
        run(800);
        shot("replay_sessions");
        for (int i = 0; i < g_model->agent_n; i++) {
            ui_open_chat(g_model->agents[i].id);
            run(1500);
            char nm[32];
            snprintf(nm, sizeof(nm), "replay_chat_%d", i);
            shot(nm);
        }
        ui_go(SCR_HOME);
        run(1000);
        shot("replay_home");
        printf("replayed %s: %d sessions\n", replay, g_model->agent_n);
        return 0;
    }

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

    // ---- boot animation, then touch setup (first boot: not calibrated yet)
    if (want_frames) frames("boot", 2800, 40);
    else {
        run(1300);
        shot("00_boot");
        run(1500);
    }
    run(400);
    expect_screen(SCR_CALIB, "touch setup");
    expect_text("Let's line up your touch");
    shot("00_calib");
    for (int i = 0; i < CALIB_POINTS; i++) tap_at(g_calib_targets[i].x, g_calib_targets[i].y);
    run(300);
    check(s_calibrated, "calibration saved after five taps");
    check(fabsf(s_cal[0] - 1) < 0.01f && fabsf(s_cal[1]) < 0.01f && fabsf(s_cal[2]) < 1 && fabsf(s_cal[3]) < 0.01f && fabsf(s_cal[4] - 1) < 0.01f &&
              fabsf(s_cal[5]) < 1,
          "calibration is the identity when raw = screen");
    expect_text("That feels better!");
    shot("00_calib_done");
    tap("Done");
    run(1200);
    expect_screen(SCR_SETUP, "setup (after touch setup)");
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

    {
        // With nowhere to start a session, the screen says how to set one up.
        dk_model_t *m = (dk_model_t *)g_model;
        int keep = m->targets_n;
        m->targets_n = 0;
        tap("+  New session");
        run(900);
        expect_text("Nowhere to start one yet");
        shot("06a_new_session_empty");
        m->targets_n = keep;
        scr_new_refresh();
        run(300);
    }
    tap("MacBook");
    run(300);
    tap("web-app");
    run(300);
    shot("06b_new_session");
    tap_obj(find_class(lv_screen_active(), &lv_textarea_class), "the prompt box");
    run(300);
    lv_textarea_set_text(find_class(lv_screen_active(), &lv_textarea_class), "Fix the flaky login test");
    tap("Start");
    run(900);
    expect_sent("\"type\":\"start\",\"target\":\"MacBook\",\"project\":\"web-app\"");
    expect_screen(SCR_SESSIONS, "sessions (after starting one)");

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

    /* A real conversation: your prompts from the computer, long replies, and live activity that
       updates the header without yanking the conversation back to the bottom. */
    drag(1180, 754, 1180, 754, 20, 0); /* the keyboard's tick closes it */
    apply("chat_long");
    run(1200);
    shot("08b_chat_conversation");
    expect_text("From your computer");
    expect_text("Research Claude Code docs");
    {
        lv_obj_t *list = lv_obj_get_parent(lv_obj_get_parent(find_label(lv_screen_active(), "From your computer")));
        int32_t before = lv_obj_get_scroll_y(list);
        drag(800, 120, 800, 440, 300, 0);
        run(800);
        bool scrolled = lv_obj_get_scroll_y(list) < before - 100;
        printf("%s  dragging the chat scrolls back through it (%d -> %d)\n", scrolled ? "PASS" : "FAIL", (int)before, (int)lv_obj_get_scroll_y(list));
        if (!scrolled) s_failures++;
        lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
        lv_obj_send_event(list, LV_EVENT_SCROLL_END, NULL);
        run(100);
        apply("chat_live");
        run(600);
        bool kept = lv_obj_get_scroll_y(list) < 40;
        printf("%s  live activity keeps your place in the chat\n", kept ? "PASS" : "FAIL");
        if (!kept) s_failures++;
        expect_text("Working · Running the sim tests");
        tap("Keep listening");
        run(300);
        expect_sent("\"type\":\"listen\"");
    }

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
    expect_screen(SCR_UPDATE, "installing an update takes over the screen");
    expect_text("Getting an upgrade");
    expect_text("v0.2.0  " LV_SYMBOL_RIGHT "  v0.3.0");
    shot("17_update_installing");
    if (want_frames) frames("update", 1500, 50);
    run(1500); // the fake download reaches 100%
    expect_text("100%");
    check(!gesture_allowed_here(), "no gestures while installing");
    ui_update_status(UI_UPDATE_DONE, 100, "v0.3.0", NULL);
    run(900);
    expect_text("All done!");
    expect_sound("done");
    shot("17b_update_done");
    ui_update_status(UI_UPDATE_FAILED, 0, NULL, "The download failed. Try again?");
    run(700);
    expect_text("Hmm, that didn't work");
    shot("17c_update_failed");
    tap("Back");
    run(600);
    expect_screen(SCR_SETTINGS, "back to settings after a failed update");
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
    drag(640, 588, 640, 300, 250, 0); // swipe up from the grabber
    run(400);
    check(!ui_gestures_panel_open(), "swipe up closes notifications");
    drag(640, 795, 640, 560, 250, 700);
    run(500);
    expect_screen(SCR_SESSIONS, "sessions (swipe up and hold)");

    // A new notification drops a banner in wherever you are; tapping one about a session opens its chat.
    apply("notify");
    run(700);
    lv_obj_t *bl = find_label(lv_layer_top(), "Claude is waiting for you");
    check(bl != NULL, "new notification shows a banner on the sessions screen");
    expect_sound("message");
    shot("19_notification_banner");
    if (bl) tap_obj(lv_obj_get_parent(lv_obj_get_parent(bl)), "notification banner");
    run(800);
    expect_screen(SCR_CHAT, "chat (tapped the banner about that session)");
    check(strcmp(g_model->feed[0].agent_id, "s-docs") == 0 && find_label(lv_screen_active(), "docs-site") != NULL, "it's that session's chat");
    ui_go(SCR_SESSIONS);
    run(500);
    ui_gestures_open_panel();
    run(600);
    check(ui_gestures_panel_open(), "notifications open");
    shot("20_notifications_list");
    // Dragging up on the list scrolls it instead of closing the sheet.
    drag(640, 450, 640, 250, 250, 0);
    run(500);
    check(ui_gestures_panel_open(), "scrolling the list keeps the sheet open");
    // Swipe one notification away.
    {
        lv_obj_t *l = find_label(lv_layer_top(), "Tests passed");
        check(l != NULL, "the list shows older notifications");
        if (l) {
            lv_area_t a;
            lv_obj_get_coords(l, &a);
            s_last_sent[0] = 0;
            drag(a.x2, (a.y1 + a.y2) / 2, a.x2 - 320, (a.y1 + a.y2) / 2 + 6, 200, 0);
            run(700);
            expect_sent("\"type\":\"feed_dismiss\"");
            check(find_label(lv_layer_top(), "Tests passed") == NULL, "a swiped notification goes away");
        }
    }
    // A notification that isn't about a session leaves the list open when tapped.
    tap_obj(find_label(lv_layer_top(), "Daily digest is ready"), "a general notification");
    run(400);
    check(ui_gestures_panel_open(), "tapping a general notification keeps the list open");
    s_last_sent[0] = 0;
    tap_obj(find_label(lv_layer_top(), "Clear all"), "Clear all");
    run(900);
    expect_sent("\"type\":\"feed_clear\"");
    shot("21_notifications_cleared");
    check(find_label(lv_layer_top(), "Claude is waiting for you") == NULL, "Clear all removes the notifications");
    drag(640, 588, 640, 300, 250, 0); // swipe up from the grabber closes it
    run(500);
    check(!ui_gestures_panel_open(), "swipe up from the grabber closes notifications");
    // Tapping a notification about a session in the list opens its chat.
    apply("notify");
    run(600);
    ui_gestures_open_panel();
    run(600);
    tap_obj(find_label(lv_layer_top(), "Permission needed"), "a session's notification");
    run(900);
    check(!ui_gestures_panel_open(), "the list closes");
    expect_screen(SCR_CHAT, "chat (tapped a notification in the list)");
    ui_go(SCR_SESSIONS);
    run(500);
    apply("busy");
    run(300);
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
