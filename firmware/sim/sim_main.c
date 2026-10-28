// Deskling UI simulator: drives the real UI code with fixture snapshots, taps buttons by
// their label, and writes screenshots (and animation frames) as PNGs.
//   deskling_sim <fixtures dir> <out dir> [--frames]
#include <dirent.h>
#include <math.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "cJSON.h"
#include "lvgl.h"
#include "model.h"
#include "ui.h"
#include "health.h"
#include "src/misc/lv_anim_private.h"
#include "src/core/lv_global.h"
#include "inbox.h"
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

/* Every label meant to be one line ("…" at the end) really is one line: none spill over the next row. */
static int one_line_bad(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return 0;
    int bad = 0;
    if (lv_obj_check_type(o, &lv_label_class) && lv_label_get_long_mode(o) == LV_LABEL_LONG_MODE_DOTS &&
        lv_obj_get_style_flex_grow(o, LV_PART_MAIN) == 0 && lv_obj_get_style_max_height(o, LV_PART_MAIN) == LV_COORD_MAX) {
        int32_t lh = lv_font_get_line_height(lv_obj_get_style_text_font(o, LV_PART_MAIN));
        if (lv_obj_get_height(o) > lh + 2) {
            printf("  wraps: \"%.40s\" (%d px high, one line is %d)\n", lv_label_get_text(o), (int)lv_obj_get_height(o), (int)lh);
            bad++;
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) bad += one_line_bad(lv_obj_get_child(o, i));
    return bad;
}

static void expect_one_line(const char *where)
{
    lv_obj_update_layout(lv_screen_active());
    int bad = one_line_bad(lv_screen_active()) + one_line_bad(lv_layer_top());
    printf("%s  one-line labels stay on one line (%s)\n", bad ? "FAIL" : "PASS", where);
    if (bad) s_failures++;
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
static bool s_rotated;
static void fake_rotate(bool on)
{
    s_rotated = on;
    printf("  -> turn the screen around %d\n", on);
}
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
static ui_memory_t s_fake_mem; // zeros = unknown, as on a board that can't tell
static char s_fresh_why[96];
static int s_fresh_screen = -1;
static void fake_memory(ui_memory_t *m) { *m = s_fake_mem; }
static void fake_fresh_start(const char *why, int screen)
{
    snprintf(s_fresh_why, sizeof(s_fresh_why), "%s", why);
    s_fresh_screen = screen;
}

static const ui_platform_t s_fake = {
    .wifi_scan = fake_scan,
    .wifi_connect = fake_connect,
    .relay_set = fake_relay_set,
    .check_update = fake_check,
    .start_update = fake_start,
    .set_brightness = fake_bright,
    .restart = fake_restart,
    .play_sound = fake_sound,
    .set_sound = fake_set_sound,
    .set_rotate_180 = fake_rotate,
    .touch_raw = fake_touch_raw,
    .touch_calibrated = fake_touch_calibrated,
    .touch_save_calibration = fake_touch_save,
    .fetch_image = fake_fetch_image,
    .memory = fake_memory,
    .fresh_start = fake_fresh_start,
};

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

/* A busy session streaming into an open chat: n snapshots, each adding a step or growing the reply. */
static double s_stream_parse, s_stream_apply;
static int s_chat_full;
static double now_ms(void);
static void stream_chat(int n)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/chat_markdown.json", s_fixtures);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = 0;
    fclose(f);
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    relativise(root, (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000);
    cJSON *agents = cJSON_GetObjectItem(root, "agents");
    cJSON *a = NULL;
    cJSON_ArrayForEach(a, agents) if (!strcmp(cJSON_GetObjectItem(a, "id")->valuestring, "s-web")) break;
    cJSON_ReplaceItemInObject(a, "state", cJSON_CreateString("working"));
    cJSON *chat = cJSON_GetObjectItem(a, "chat");
    static dk_model_t m;
    char id[16], text[200];
    for (int i = 0; i < n; i++) {
        if (i % 3 == 2) {
            // the reply streams: its text grows in place
            cJSON *last = cJSON_GetArrayItem(chat, cJSON_GetArraySize(chat) - 1);
            const char *old = cJSON_GetObjectItem(last, "text")->valuestring;
            char *grown = malloc(strlen(old) + 64);
            sprintf(grown, "%s\n- and point %d", old, i);
            cJSON_ReplaceItemInObject(last, "text", cJSON_CreateString(grown));
            free(grown);
        } else {
            cJSON *l = cJSON_CreateObject();
            snprintf(id, sizeof(id), "st%d", i);
            snprintf(text, sizeof(text), "Reading src/file%d.ts", i);
            cJSON_AddStringToObject(l, "id", id);
            cJSON_AddStringToObject(l, "from", "tool");
            cJSON_AddStringToObject(l, "text", text);
            cJSON_AddStringToObject(l, "status", "sent");
            cJSON_InsertItemInArray(chat, cJSON_GetArraySize(chat) - 1, l);
            if (cJSON_GetArraySize(chat) > 30) cJSON_DeleteItemFromArray(chat, 0);
        }
        char *json = cJSON_PrintUnformatted(root);
        double t0 = now_ms();
        if (dk_model_parse(json, strlen(json), &m)) {
            double t1 = now_ms();
            ui_apply_model(&m);
            s_stream_parse += t1 - t0;
            s_stream_apply += now_ms() - t1;
        }
        free(json);
        run(100);
    }
    cJSON_Delete(root);
    printf("  parsing %.1f ms, applying %.1f ms over %d snapshots\n", s_stream_parse, s_stream_apply, n);
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
    {
        struct mallinfo2 mi0 = mallinfo2();
        SCENE("open a long Markdown chat", 900, (apply("chat_markdown"), ui_open_chat("s-web"), run(900)));
        struct mallinfo2 mi1 = mallinfo2();
        printf("  heap used by the open chat: %+ld KB\n", ((long)mi1.uordblks - (long)mi0.uordblks) / 1024);
        SCENE("chat streaming, 30 snapshots", 3000, stream_chat(30));
        SCENE("chat idle 2 s", 2000, run(2000));
        SCENE("open a heavy chat (long replies, pictures)", 1500, (apply("chat_heavy"), ui_open_chat("s-web"), run(1500)));
        SCENE("scroll the heavy chat", 1600, (drag(800, 150, 800, 560, 500, 0), run(300), drag(800, 560, 800, 150, 500, 0), run(300)));
        SCENE("scroll the chat up and down", 1600, (drag(800, 150, 800, 560, 500, 0), run(300), drag(800, 560, 800, 150, 500, 0), run(300)));
    }
    SCENE("chat -> sessions (slide)", 700, (ui_go(SCR_SESSIONS), run(700)));
    SCENE("sessions -> quests (fade)", 600, (apply("quests"), ui_go(SCR_QUESTS), run(600)));
    SCENE("quests, idle 2 s", 2000, run(2000));
    SCENE("scroll the quests", 1600, (drag(820, 600, 820, 200, 500, 0), run(300), drag(820, 200, 820, 600, 500, 0), run(300)));
    SCENE("boss, combo, level-up celebrations", 9000, (apply("quests_win"), run(600), (getenv("DK_AREAS") ? (puts("  frames:"), 0) : 0), (getenv("DK_AREAS") ? (puts("  boss celebration, one frame:"), s_dump_areas = 1, run(17), s_dump_areas = 0) : 0), run(8383)));
    SCENE("focus timer, idle 3 s", 3000, (apply("quests_focus"), run(3000)));
    if (getenv("DK_AREAS")) { puts("  focus idle, one frame:"); s_dump_areas = 1; run(17); s_dump_areas = 0; }
    SCENE("quests -> home (fade)", 600, (ui_go(SCR_HOME), run(600)));
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


// ---- chaos: random snapshots (sessions coming and going, huge and odd text, requests for
// sessions that don't exist, too many of everything) while moving between screens and tapping
// around. Run under AddressSanitizer (tools/run_sim.sh with DESKLING_SIM_ASAN=1) to catch crashes.
static uint32_t s_rng = 12345;
static uint32_t rnd(uint32_t n)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return n ? (s_rng >> 8) % n : 0;
}
static void rnd_text(char *out, size_t cap, size_t max)
{
    static const char *bits[] = {"fix ", "the ", "login ", "flow ", "é", "漢字", "😀 ", "\n", "\"quoted\" ", "C:\\path\\x ", "<b>", "&amp; ", "  ", "a", "%s %d ", "verylongwordwithoutanyspacesatallthatkeepsgoing ",
                                 /* Markdown, whole and broken */
                                 "**", "`", "```\n", "# ", "\n## ", "\n- ", "\n  - ", "\n1. ", "\n> ", "|", "\n| a | b |\n| --- | --- |\n", "\n---\n", "[x](", "](y)", "~~", "_", "*", "\\", "    "};
    size_t len = 0, want = rnd((uint32_t)max + 1);
    out[0] = 0;
    while (len < want && len + 64 < cap) {
        const char *b = bits[rnd(sizeof(bits) / sizeof(bits[0]))];
        size_t l = strlen(b);
        memcpy(out + len, b, l);
        len += l;
        out[len] = 0;
    }
}
static const char *pick(const char *const *list, size_t n) { return list[rnd((uint32_t)n)]; }
#define PICK(list) pick(list, sizeof(list) / sizeof(list[0]))

static void chaos(int rounds)
{
    static const char *sources[] = {"claude-code", "cursor", "codex", "gemini", "opencode", "custom", "weird"};
    static const char *states[] = {"idle", "working", "waiting", "needs_you", "done", "bogus"};
    static const char *froms[] = {"you", "agent", "tool", "thought"};
    static const char *statuses[] = {"queued", "delivered", "typed", "sent"};
    static const char *causes[] = {"hello", "agent", "chat", "done", "permission", "question", "feed", "reminder", "reminder_due", "ping", "permission_closed"};
    static const char *screens_ok[] = {"home", "sessions", "chat", "new", "night"};
    static char text[4096];
    char id[32];
    for (int r = 0; r < rounds; r++) {
        cJSON *root = cJSON_CreateObject();
        struct timeval tv;
        gettimeofday(&tv, NULL);
        double now = (double)tv.tv_sec * 1000 + tv.tv_usec / 1000;
        cJSON_AddStringToObject(root, "type", "snapshot");
        cJSON_AddNumberToObject(root, "now", now);
        cJSON *cause = cJSON_AddObjectToObject(root, "cause");
        cJSON_AddStringToObject(cause, "kind", PICK(causes));
        snprintf(id, sizeof(id), "ag%u", rnd(16));
        cJSON_AddStringToObject(cause, "id", id);
        cJSON *agents = cJSON_AddArrayToObject(root, "agents");
        int na = (int)rnd(15);
        for (int i = 0; i < na; i++) {
            cJSON *a = cJSON_CreateObject();
            snprintf(id, sizeof(id), "ag%u", rnd(16));
            cJSON_AddStringToObject(a, "id", id);
            cJSON_AddStringToObject(a, "source", PICK(sources));
            rnd_text(text, sizeof(text), 90);
            cJSON_AddStringToObject(a, "name", text);
            cJSON_AddStringToObject(a, "host", rnd(2) ? "MacBook" : "");
            cJSON_AddStringToObject(a, "os", "mac");
            cJSON_AddStringToObject(a, "state", PICK(states));
            rnd_text(text, sizeof(text), 200);
            cJSON_AddStringToObject(a, "task", text);
            rnd_text(text, sizeof(text), 400);
            cJSON_AddStringToObject(a, "lastMessage", text);
            cJSON_AddNumberToObject(a, "updated", now - rnd(600000));
            cJSON_AddBoolToObject(a, "canMessage", rnd(2));
            cJSON_AddBoolToObject(a, "listening", rnd(2));
            if (rnd(2)) cJSON_AddStringToObject(a, "model", "ollama/qwen3-coder:30b-a-very-long-model-name-indeed");
            cJSON_AddNumberToObject(a, "context", rnd(900000));
            cJSON *chat = cJSON_AddArrayToObject(a, "chat");
            int nc = (int)rnd(45);
            for (int c = 0; c < nc; c++) {
                cJSON *l = cJSON_CreateObject();
                snprintf(id, sizeof(id), "c%u", rnd(60));
                cJSON_AddStringToObject(l, "id", id);
                cJSON_AddStringToObject(l, "from", PICK(froms));
                rnd_text(text, sizeof(text), rnd(8) ? 200 : 3000);
                cJSON_AddStringToObject(l, "text", text);
                cJSON_AddStringToObject(l, "status", PICK(statuses));
                cJSON_AddNumberToObject(l, "ts", now - rnd(60000));
                if (!rnd(8)) cJSON_AddStringToObject(l, "image", rnd(2) ? "img1" : "nope");
                if (!rnd(4)) cJSON_AddBoolToObject(l, "more", true);
                if (!rnd(6)) cJSON_AddNumberToObject(l, "hidden", rnd(3) ? rnd(30) : -5);
                if (!rnd(8)) {
                    rnd_text(text, sizeof(text), 600);
                    cJSON_AddStringToObject(l, "quote", text);
                }
                cJSON_AddItemToArray(chat, l);
            }
            cJSON *helpers = cJSON_AddArrayToObject(a, "helpers");
            for (int h = 0, nh = (int)rnd(7); h < nh; h++) {
                cJSON *x = cJSON_CreateObject();
                snprintf(id, sizeof(id), "h%d", h);
                cJSON_AddStringToObject(x, "id", id);
                rnd_text(text, sizeof(text), 120);
                cJSON_AddStringToObject(x, "name", text);
                cJSON_AddStringToObject(x, "task", "Reading");
                cJSON_AddBoolToObject(x, "done", rnd(2));
                cJSON_AddItemToArray(helpers, x);
            }
            cJSON_AddItemToArray(agents, a);
        }
        cJSON *pending = cJSON_AddArrayToObject(root, "pending");
        for (int i = 0, np = (int)rnd(4); i < np; i++) {
            cJSON *p = cJSON_CreateObject();
            snprintf(id, sizeof(id), "p%u", rnd(5));
            cJSON_AddStringToObject(p, "id", id);
            bool q = rnd(2);
            cJSON_AddStringToObject(p, "kind", q ? "question" : "permission");
            snprintf(id, sizeof(id), "ag%u", rnd(20));
            cJSON_AddStringToObject(p, "agentId", id);
            cJSON_AddStringToObject(p, "source", PICK(sources));
            cJSON_AddStringToObject(p, "name", "web-app");
            cJSON_AddStringToObject(p, "host", "MacBook");
            cJSON_AddStringToObject(p, "tool", rnd(2) ? "Bash" : "SomeVeryLongToolNameThatGoesOnAndOn");
            rnd_text(text, sizeof(text), 500);
            cJSON_AddStringToObject(p, "summary", text);
            cJSON_AddStringToObject(p, "detail", "detail");
            cJSON_AddNumberToObject(p, "expires", now + rnd(120000));
            cJSON *qs = cJSON_AddArrayToObject(p, "questions");
            for (int k = 0, nq = q ? (int)rnd(6) : 0; k < nq; k++) {
                cJSON *qq = cJSON_CreateObject();
                rnd_text(text, sizeof(text), 200);
                cJSON_AddStringToObject(qq, "question", text);
                cJSON_AddBoolToObject(qq, "multiSelect", rnd(2));
                cJSON *opts = cJSON_AddArrayToObject(qq, "options");
                for (int o = 0, no = (int)rnd(7); o < no; o++) {
                    cJSON *op = cJSON_CreateObject();
                    rnd_text(text, sizeof(text), 80);
                    cJSON_AddStringToObject(op, "label", text);
                    cJSON_AddStringToObject(op, "description", "");
                    cJSON_AddItemToArray(opts, op);
                }
                cJSON_AddItemToArray(qs, qq);
            }
            cJSON_AddItemToArray(pending, p);
        }
        cJSON *feed = cJSON_AddArrayToObject(root, "feed");
        for (int i = 0, nf = (int)rnd(16); i < nf; i++) {
            cJSON *f = cJSON_CreateObject();
            snprintf(id, sizeof(id), "f%d", i);
            cJSON_AddStringToObject(f, "id", id);
            cJSON_AddStringToObject(f, "level", rnd(2) ? "alert" : "good");
            rnd_text(text, sizeof(text), 120);
            cJSON_AddStringToObject(f, "title", text);
            cJSON_AddStringToObject(f, "body", "body");
            snprintf(id, sizeof(id), "ag%u", rnd(20));
            if (rnd(2)) cJSON_AddStringToObject(f, "agentId", id);
            cJSON_AddNumberToObject(f, "ts", now - rnd(100000));
            cJSON_AddItemToArray(feed, f);
        }
        cJSON *rem = cJSON_AddArrayToObject(root, "reminders");
        for (int i = 0, nr = (int)rnd(16); i < nr; i++) {
            cJSON *x = cJSON_CreateObject();
            snprintf(id, sizeof(id), "r%d", i);
            cJSON_AddStringToObject(x, "id", id);
            rnd_text(text, sizeof(text), 120);
            cJSON_AddStringToObject(x, "title", text);
            cJSON_AddNumberToObject(x, "due", now + (double)rnd(200000) - 100000);
            cJSON_AddStringToObject(x, "from", "you");
            cJSON_AddBoolToObject(x, "done", rnd(2));
            cJSON_AddItemToArray(rem, x);
        }
        cJSON *targets = cJSON_AddArrayToObject(root, "targets");
        for (int i = 0, nt = (int)rnd(9); i < nt; i++) {
            cJSON *t = cJSON_CreateObject();
            snprintf(id, sizeof(id), "t%d", i);
            cJSON_AddStringToObject(t, "id", id);
            rnd_text(text, sizeof(text), 80);
            cJSON_AddStringToObject(t, "name", text);
            cJSON_AddStringToObject(t, "kind", i == 0 ? "cloud" : "computer");
            cJSON *pj = cJSON_AddArrayToObject(t, "projects");
            for (int k = 0, n = (int)rnd(16); k < n; k++) {
                rnd_text(text, sizeof(text), 70);
                cJSON_AddItemToArray(pj, cJSON_CreateString(text));
            }
            cJSON *en = cJSON_AddArrayToObject(t, "engines");
            for (int k = 0, n = (int)rnd(11); k < n; k++) {
                cJSON *e = cJSON_CreateObject();
                snprintf(id, sizeof(id), "e%d", k);
                cJSON_AddStringToObject(e, "id", k == 0 ? "claude" : id);
                rnd_text(text, sizeof(text), 40);
                cJSON_AddStringToObject(e, "name", text);
                cJSON *ms = cJSON_AddArrayToObject(e, "models");
                for (int j = 0, nm = (int)rnd(11); j < nm; j++) {
                    cJSON *m = cJSON_CreateObject();
                    rnd_text(text, sizeof(text), 90);
                    cJSON_AddStringToObject(m, "id", text);
                    if (rnd(2)) cJSON_AddStringToObject(m, "name", text);
                    cJSON_AddItemToArray(ms, m);
                }
                cJSON_AddItemToArray(en, e);
            }
            cJSON_AddItemToArray(targets, t);
        }
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        static dk_model_t m;
        if (rnd(2)) {
            inbox_push(json, strlen(json)); // the board's way: drawn on the screen's own turn
            if (rnd(2)) inbox_push(json, strlen(json));
            inbox_pump();
        } else if (dk_model_parse(json, strlen(json), &m)) {
            ui_apply_model(&m);
        }
        free(json);
        run(rnd(250));

        // The answer to "Show all" or opened steps, for whatever chat is open (or a stale one).
        if (!rnd(3)) {
            cJSON *d = cJSON_CreateObject();
            cJSON_AddStringToObject(d, "type", "detail");
            snprintf(id, sizeof(id), "ag%u", rnd(16));
            cJSON_AddStringToObject(d, "agentId", rnd(2) ? scr_chat_agent() : id);
            snprintf(id, sizeof(id), "c%u", rnd(60));
            cJSON_AddStringToObject(d, "lineId", id);
            if (rnd(2)) {
                rnd_text(text, sizeof(text), 4000);
                cJSON_AddStringToObject(d, "text", text);
            } else {
                cJSON *st = cJSON_AddArrayToObject(d, "steps");
                for (int k = 0, n = (int)rnd(40); k < n; k++) {
                    cJSON *x = cJSON_CreateObject();
                    cJSON_AddStringToObject(x, "from", rnd(2) ? "tool" : "thought");
                    rnd_text(text, sizeof(text), 200);
                    cJSON_AddStringToObject(x, "text", text);
                    cJSON_AddItemToArray(st, x);
                }
            }
            char *dj = cJSON_PrintUnformatted(d);
            cJSON_Delete(d);
            if (rnd(2)) {
                inbox_push(dj, strlen(dj));
            } else {
                dk_detail_t det;
                if (dk_detail_parse(dj, strlen(dj), &det)) ui_chat_detail(&det);
                dk_detail_free(&det);
            }
            free(dj);
            run(rnd(120));
        }

        // Move around and poke at things (never Settings: it can restart or update).
        switch (rnd(6)) {
        case 0: {
            const char *where = PICK(screens_ok);
            if (!strcmp(where, "home")) ui_go(SCR_HOME);
            else if (!strcmp(where, "sessions")) ui_go(SCR_SESSIONS);
            else if (!strcmp(where, "new")) ui_go(SCR_NEW);
            else if (!strcmp(where, "night")) ui_go(SCR_NIGHT);
            else {
                snprintf(id, sizeof(id), "ag%u", rnd(16));
                ui_open_chat(id);
            }
            break;
        }
        case 1:
        case 2: {
            scr_id_t cur = ui_current();
            if (cur == SCR_HOME || cur == SCR_SESSIONS || cur == SCR_CHAT || cur == SCR_NEW || cur == SCR_ALERT)
                tap_at(40 + (int)rnd(W - 80), 100 + (int)rnd(H - 200)); // away from the rail and edges
            break;
        }
        default:
            break;
        }
        if (ui_current() == SCR_SETTINGS || ui_current() == SCR_UPDATE || ui_current() == SCR_SETUP || ui_current() == SCR_CALIB) ui_go(SCR_HOME);
    }
}

static uint32_t count_objs(lv_obj_t *o)
{
    uint32_t n = 1;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) n += count_objs(lv_obj_get_child(o, i));
    return n;
}

/* apply() without the log line (the soak test applies thousands). */
static void silent_apply(const char *fixture)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.json", s_fixtures, fixture);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1);
    fread(buf, 1, n, f);
    buf[n] = 0;
    fclose(f);
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    relativise(root, (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000);
    // Every update brings new chat lines and notifications (as a real night does), not the same ones.
    static unsigned serial;
    serial++;
    cJSON *ag, *ln, *fd;
    cJSON_ArrayForEach(ag, cJSON_GetObjectItem(root, "agents"))
    {
        cJSON_ArrayForEach(ln, cJSON_GetObjectItem(ag, "chat"))
        {
            cJSON *id = cJSON_GetObjectItem(ln, "id");
            if (cJSON_IsString(id)) {
                char nid[40];
                snprintf(nid, sizeof(nid), "%.20s-%u", id->valuestring, serial % 7 == 0 ? serial : serial / 7);
                cJSON_SetValuestring(id, nid);
            }
        }
    }
    cJSON_ArrayForEach(fd, cJSON_GetObjectItem(root, "feed"))
    {
        cJSON *id = cJSON_GetObjectItem(fd, "id");
        if (cJSON_IsString(id)) {
            char nid[40];
            snprintf(nid, sizeof(nid), "%.20s-%u", id->valuestring, serial / 3);
            cJSON_SetValuestring(id, nid);
        }
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    inbox_push(json, strlen(json)); // the board's way in
    free(json);
}

/* Looping animations on objects of `scr`: how many, and how many of them are paused. */
static void loops_on(lv_obj_t *scr, int *n, int *paused)
{
    *n = *paused = 0;
    lv_ll_t *ll = &LV_GLOBAL_DEFAULT()->anim_state.anim_ll;
    for (lv_anim_t *a = lv_ll_get_head(ll); a; a = lv_ll_get_next(ll, a)) {
        if (a->repeat_cnt != LV_ANIM_REPEAT_INFINITE || !a->var || !lv_obj_is_valid(a->var)) continue;
        if (lv_obj_get_screen(a->var) != scr) continue;
        (*n)++;
        *paused += a->is_paused;
    }
}

static void housekeeping_tests(void)
{
    // The fresh-start decision.
    {
        health_in_t in = {.uptime_s = 3 * 3600, .hour = 14, .idle_s = 600, .calm = true};
        check(!health_verdict(&in).problem, "health: nothing to do on a healthy board");
        in.mem = (ui_memory_t){.internal_free_kb = 20, .internal_largest_kb = 8, .psram_free_kb = 20000, .psram_largest_kb = 16000};
        health_verdict_t v = health_verdict(&in);
        check(v.problem && v.restart_now, "health: starts fresh when internal memory runs out and nobody's using it");
        in.calm = false;
        check(!health_verdict(&in).restart_now, "health: ...but not while something's on screen for you");
        in.calm = true, in.idle_s = 20;
        check(!health_verdict(&in).restart_now, "health: ...nor while you're using it");
        in.idle_s = 600, in.uptime_s = 120;
        check(!health_verdict(&in).restart_now, "health: ...nor straight after starting (no restart loops)");
        in = (health_in_t){.uptime_s = 22 * 3600, .hour = 4, .idle_s = 3600, .calm = true};
        check(health_verdict(&in).restart_now, "health: a nightly fresh start after a long run");
        in.hour = 9;
        check(!health_verdict(&in).problem, "health: not in the daytime");
        in.hour = -1;
        check(!health_verdict(&in).problem, "health: not without a clock");
        in = (health_in_t){.uptime_s = 5 * 3600, .hour = 1, .idle_s = 3600, .calm = true, .slow_windows = 3};
        check(health_verdict(&in).restart_now, "health: starts fresh when the night clock has become slow");
        in = (health_in_t){.uptime_s = 5 * 3600, .hour = 14, .idle_s = 3600, .calm = true, .objects = 20000};
        check(health_verdict(&in).restart_now, "health: starts fresh when things pile up on the screens");
    }

    // Loops on screens you can't see are paused, and pick up again when the screen is back.
    apply("busy");
    ui_go(SCR_HOME);
    run(800);
    int n, paused;
    loops_on(ui_screen(SCR_NIGHT), &n, &paused);
    check(n > 0 && paused == n, "the night clock's Deskling rests while the night clock is hidden");
    loops_on(ui_screen(SCR_HOME), &n, &paused);
    check(n > 0 && paused == 0, "home's animations keep going");
    ui_go(SCR_NIGHT);
    run(900);
    loops_on(ui_screen(SCR_NIGHT), &n, &paused);
    check(n > 0 && paused == 0, "the night clock's Deskling moves again when it's shown");
    loops_on(ui_screen(SCR_HOME), &n, &paused);
    check(n > 0 && paused == n, "home rests behind the night clock");
    ui_go(SCR_HOME);
    run(900);

    // A chat you left ten minutes ago is let go of, and comes back whole when you open it.
    apply("chat_long");
    ui_open_chat("s-web");
    run(900);
    uint32_t full = count_objs(ui_screen(SCR_CHAT));
    check(find_label(lv_screen_active(), "the device is stuck on a black screen, did we brick it?") != NULL, "the chat shows its messages");
    ui_go(SCR_HOME);
    uint32_t before = ui_health()->chats_released;
    for (int i = 0; i < 11 * 12; i++) run(5000);
    uint32_t let_go = count_objs(ui_screen(SCR_CHAT));
    printf("  chat screen: %u things open, %u after letting go\n", full, let_go);
    check(ui_health()->chats_released == before + 1, "a chat left for ten minutes is let go of (once)");
    check(let_go + 40 < full && !find_label(ui_screen(SCR_CHAT), "the device is stuck on a black screen, did we brick it?"), "...its bubbles are gone");
    ui_open_chat("s-web");
    run(900);
    check(find_label(lv_screen_active(), "the device is stuck on a black screen, did we brick it?") != NULL, "...and come back when you open it again");
    ui_go(SCR_HOME);
    run(900);

    // Memory running out: a fresh start, but only when nobody's using it and nothing's asking.
    while (lv_tick_get() < 11 * 60 * 1000) run(5000);
    s_fake_mem = (ui_memory_t){.internal_free_kb = 18, .internal_largest_kb = 6, .psram_free_kb = 20000, .psram_largest_kb = 16000};
    apply("permission");
    for (int i = 0; i < 30; i++) run(5000);
    check(s_fresh_screen < 0, "no fresh start while a permission question is up");
    check(strstr(ui_health()->problem, "memory") != NULL, "...but the board's page says what's wrong");
    apply("busy");
    ui_go(SCR_HOME);
    for (int i = 0; i < 30; i++) run(5000);
    check(s_fresh_screen == SCR_HOME && strstr(s_fresh_why, "memory"), "a fresh start once it's calm and untouched, back to home");
    s_fake_mem = (ui_memory_t){0};
}

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
    // DK_QUIET_START: start as after a fresh start that was on the night clock.
    bool quiet = getenv("DK_QUIET_START") != NULL;
    if (quiet) ui_set_quiet_start(SCR_NIGHT);
    ui_init();
    ui_set_platform(&s_fake);
    if (quiet) {
        run(300);
        expect_screen(SCR_NIGHT, "night clock (straight back after a fresh start, no boot animation)");
        return s_failures ? 1 : 0;
    }

    // DK_SOAK=<minutes>: leave the board running (simulated time) with updates every few seconds
    // and the screen changing now and then, like a night on the desk, and print what it's holding
    // every 10 minutes: running animations, timers, objects and heap. Anything that only grows is
    // a leak that slows the board down over time.
    const char *soak = getenv("DK_SOAK");
    if (soak) {
        ui_set_connection(DK_CONN_ONLINE, NULL);
        s_calibrated = true;
        run(3500);
        static const char *fx[] = {"busy", "chat_long", "chat_live", "chat_markdown", "quests", "notify", "done", "opencode", "reminder_due", "chat_waiting", "quests_focus", "busy_long"};
        static const scr_id_t scr[] = {SCR_HOME, SCR_SESSIONS, SCR_CHAT, SCR_QUESTS, SCR_HOME, SCR_NIGHT, SCR_HOME, SCR_CHAT};
        int minutes = atoi(soak);
        int nfx = (int)(sizeof(fx) / sizeof(fx[0]));
        // The most it held in the first half hour (one turn round every screen and fixture), and
        // the most after that: anything that keeps growing ends well above the first.
        uint32_t warm[3] = {0}, late[3] = {0};
        for (int m = 0; m <= minutes; m++) {
            {
                struct mallinfo2 mi = mallinfo2();
                uint32_t now[3] = {lv_anim_count_running(), health_count_objects(), (uint32_t)(mi.uordblks / 1024)};
                for (int i = 0; i < 3; i++) {
                    uint32_t *into = m < 30 ? warm : late;
                    if (now[i] > into[i]) into[i] = now[i];
                }
            }
            if (m % 10 == 0) {
                int timers = 0;
                for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)) timers++;
                struct mallinfo2 mi = mallinfo2();
                const ui_health_t *h = ui_health();
                printf("soak %4d min: anims %4u (%3u paused)  timers %3d  objects %5u  heap %7zu KB  chats let go %u%s%s\n", m,
                       lv_anim_count_running(), h->paused, timers, health_count_objects(), mi.uordblks / 1024, h->chats_released,
                       s_fresh_why[0] ? "  fresh start: " : "", s_fresh_why);
                fflush(stdout);
            }
            for (int k = 0; k < 20; k++) { // an update every 3 s
                silent_apply(fx[(m * 20 + k) % nfx]);
                run(3000);
            }
            scr_id_t to = scr[m % (int)(sizeof(scr) / sizeof(scr[0]))];
            if (to == SCR_CHAT) ui_open_chat("s-web");
            else ui_go(to);
        }
        if (minutes >= 40) {
            static const char *what[] = {"animations", "things on the screens", "memory use (KB)"};
            for (int i = 0; i < 3; i++) {
                bool ok = late[i] <= warm[i] + warm[i] / 4 + 8;
                printf("%s  no build-up of %s over %d minutes (first half hour at most %u, after that at most %u)\n", ok ? "PASS" : "FAIL",
                       what[i], minutes, warm[i], late[i]);
                if (!ok) s_failures++;
            }
        }
        return s_failures ? 1 : 0;
    }

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
    apply("busy_long");
    run(1200);
    shot("03b_home_long_feed");
    expect_one_line("home, long notifications");
    apply("busy");
    run(600);
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
    expect_one_line("sessions");

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
    expect_sent("\"type\":\"start\",\"target\":\"MacBook\",\"project\":\"web-app\",\"prompt\":\"Fix the flaky login test\",\"engine\":\"claude\"");
    expect_screen(SCR_SESSIONS, "sessions (after starting one)");

    // OpenCode, with a local model.
    tap("+  New session");
    run(900);
    tap("OpenCode");
    run(300);
    expect_text("WHAT SHOULD OPENCODE DO?");
    tap("Qwen3 Coder 30B");
    run(300);
    shot("06c_new_session_opencode");
    lv_textarea_set_text(find_class(lv_screen_active(), &lv_textarea_class), "Tidy the CSS");
    tap("Start");
    run(900);
    expect_sent("\"engine\":\"opencode\",\"model\":\"ollama/qwen3-coder:30b\"");
    expect_screen(SCR_SESSIONS, "sessions (after starting OpenCode)");

    // Another agent the runner speaks ACP with: no models to pick.
    tap("+  New session");
    run(900);
    tap("Gemini CLI");
    run(300);
    expect_text("WHAT SHOULD GEMINI CLI DO?");
    lv_textarea_set_text(find_class(lv_screen_active(), &lv_textarea_class), "Write the changelog");
    tap("Start");
    run(900);
    expect_sent("\"engine\":\"gemini\"}");

    tap("web-app");
    run(900);
    shot("07_chat");
    expect_sent("\"type\":\"view\",\"agentId\":\"s-web\"");
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
        tap("Wait for my reply");
        run(300);
        expect_sent("\"type\":\"listen\"");
    }

    /* Replies in Markdown, "Show all" for a long one, and steps you can open. */
    apply("chat_markdown");
    run(900);
    shot("08d_chat_markdown");
    check(find_class(lv_screen_active(), &lv_spangroup_class) != NULL, "a reply is drawn as formatted text");
    expect_text("Show all");
    tap("9 earlier steps");
    run(200);
    expect_sent("\"type\":\"detail\",\"agentId\":\"s-web\",\"lineId\":\"m2\"");
    expect_text("Loading the steps...");
    {
        const char *j = "{\"type\":\"detail\",\"agentId\":\"s-web\",\"lineId\":\"m2\",\"steps\":["
                        "{\"from\":\"tool\",\"text\":\"Reading net.c\"},{\"from\":\"thought\",\"text\":\"The installer wrote at 0x0.\"},"
                        "{\"from\":\"tool\",\"text\":\"Searching for merge-bin\"}]}";
        dk_detail_t d;
        check(dk_detail_parse(j, strlen(j), &d) && d.steps_n == 3, "a detail message parses");
        ui_chat_detail(&d);
        dk_detail_free(&d);
    }
    run(400);
    expect_text("Reading net.c");
    expect_text("Searching for merge-bin");
    shot("08e_chat_steps_open");
    tap("9 earlier steps");
    run(300);
    check(!find_label(lv_screen_active(), "Reading net.c"), "tapping the steps again folds them away");
    expect_text("On for 24 min");
    expect_text("Claude has already stopped, so this starts the next time it finishes.");
    ui_open_chat("s-web"); /* from the top: opening a chat starts at its newest line */
    run(600);
    shot("08d2_chat_markdown_end");
    tap("Show all");
    run(300);
    expect_sent("\"lineId\":\"m6\"");
    expect_text("Loading the whole reply...");
    {
        static char j[4096];
        snprintf(j, sizeof(j), "{\"type\":\"detail\",\"agentId\":\"s-web\",\"lineId\":\"m6\",\"text\":\"%s\"}",
                 "# The whole reply\\n\\nEverything Claude wrote, with **nothing** cut.\\n\\n## Steps\\n\\n1. One\\n2. Two with `code`\\n\\n| A | B |\\n| - | - |\\n| 1 | 2 |\\n\\nThe end.");
        dk_detail_t d;
        dk_detail_parse(j, strlen(j), &d);
        ui_chat_detail(&d);
        dk_detail_free(&d);
    }
    run(500);
    shot("08f_chat_reader");
    expect_text("Back to the chat");
    check(ui_back() && ui_current() == SCR_CHAT, "back closes the reader first");
    run(300);
    check(!find_label(lv_screen_active(), "Back to the chat"), "the reader is closed");

    /* Snapshots arriving faster than they can be drawn: only the newest is drawn, and an answer
       you asked for gets through first. */
    {
        inbox_stats_t before = inbox_stats();
        static const char *fx[] = {"chat_long", "chat_markdown", "chat_waiting"};
        for (int i = 0; i < 3; i++) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s.json", s_fixtures, fx[i]);
            FILE *f = fopen(path, "rb");
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *buf = malloc(n + 1);
            fread(buf, 1, n, f);
            buf[n] = 0;
            fclose(f);
            cJSON *root = cJSON_Parse(buf);
            struct timeval tv;
            gettimeofday(&tv, NULL);
            relativise(root, (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000);
            char *json = cJSON_PrintUnformatted(root);
            inbox_push(json, strlen(json));
            free(json);
            cJSON_Delete(root);
            free(buf);
        }
        const char *dj = "{\"type\":\"detail\",\"agentId\":\"s-web\",\"lineId\":\"nope\",\"text\":\"x\"}";
        inbox_push(dj, strlen(dj));
        run(400); // the inbox's own timer hands them over
        inbox_stats_t after = inbox_stats();
        check(after.received - before.received == 3 && after.drawn - before.drawn == 1, "three snapshots at once: only the newest is drawn");
        check(after.details - before.details == 1, "an answer gets through alongside");
        expect_text("Claude is waiting for your reply"); // chat_waiting was the newest
    }

    /* Wait for my reply: while Claude is held waiting, the chat says so. */
    apply("chat_waiting");
    run(900);
    shot("08g_chat_waiting");
    expect_text("Claude is waiting for your reply");
    expect_text("Stop waiting");
    expect_text("Type below or tap a quick reply. It waits up to 24 min more.");
    expect_text("Your turn");
    tap("Stop waiting");
    run(200);
    expect_sent("\"type\":\"listen\",\"agentId\":\"s-web\",\"minutes\":0");

    /* Quests: tickets from Mojo Desk, Linear and Jira as a game. */
    {
        uint32_t played0 = ui_celebrations_played();
        apply("quests");
        ui_go(SCR_HOME);
        run(900);
        expect_text("Lv 4");
        shot("19_home_quests_chip");
        check(ui_celebrations_played() == played0, "old news isn't replayed when the score is first seen");
        tap("Lv 4");
        run(800);
        expect_screen(SCR_QUESTS, "quests");
        shot("20_quests");
        expect_text("Today's quests");
        expect_text("1 of 4 done");
        expect_text("Level 4");
        expect_text("5 day streak");
        expect_text("Combo x2");
        expect_text("MORE QUESTS");
        expect_one_line("quests");
        tap("Rate limit the login endpoint");
        run(500);
        shot("20b_quest_open");
        expect_text("Focus 25 min");
        tap("Start");
        run(200);
        expect_sent("\"type\":\"quest_status\",\"id\":\"linear:lin-1\",\"to\":\"progress\"");
        tap("Hand to Claude");
        run(600);
        expect_screen(SCR_NEW, "new session, from a quest");
        {
            lv_obj_t *ta = find_class(lv_screen_active(), &lv_textarea_class);
            check(ta && strncmp(lv_textarea_get_text(ta), "Work on ENG-12 (Linear)", 23) == 0, "Hand to Claude starts a session with the ticket");
            if (ta) lv_textarea_set_text(ta, "");
        }
        ui_go(SCR_QUESTS);
        run(700);
        {
            // The first quest's check (the last thing on its top row): once arms it...
            lv_obj_t *t = find_label(lv_screen_active(), "Integrations");
            lv_obj_t *row = t ? lv_obj_get_parent(lv_obj_get_parent(t)) : NULL;
            tap_obj(row ? lv_obj_get_child(row, -1) : NULL, "the quest's check");
        }
        run(200);
        expect_text("Tap: done");
        s_last_sent[0] = 0;
        tap("Tap: done"); // ...twice finishes it
        run(200);
        expect_sent("\"type\":\"quest_status\",\"id\":\"mojo:DKL-30\",\"to\":\"done\"");
        tap("Add " LV_SYMBOL_PLUS);
        run(200);
        expect_sent("\"type\":\"quest_plan\"");

        // A boss falls, a combo, and a level-up with an unlock: they play one after another.
        apply("quests_win");
        run(500);
        shot("21_boss_defeated");
        expect_text("Boss defeated!");
        run(2400);
        shot("21b_combo");
        run(1900);
        shot("22_level_up");
        expect_text("Level 5!");
        expect_text("Unlocked focus headphones");
        run(3600);
        check(ui_celebrations_played() - played0 >= 3, "the boss, the combo and the level-up all play");
        shot("22b_quests_after");

        apply("quests_focus");
        run(800);
        shot("23_focus");
        expect_text("Focusing on");
        expect_text("Boss HP");
        tap("Stop focusing");
        run(200);
        expect_sent("\"type\":\"quest_focus\",\"stop\":true");

        apply("quests_cleared");
        run(600);
        shot("24_day_cleared");
        expect_text("Day cleared!");
        run(3200);
        shot("24b_challenge");
        expect_text("Weekly challenge complete");
        run(2600);

        apply("quests_error");
        run(500);
        expect_text("Jira didn't accept the key");
        shot("25_quests_error");
        apply("quests_empty");
        run(500);
        expect_text("Turn your tickets into quests");
        shot("26_quests_empty");
        check(ui_back() && ui_current() == SCR_QUESTS, "back first closes the open quest's actions");
        check(ui_back() && ui_current() == SCR_HOME, "then goes home");
        run(400);
    }

    // OpenCode: its own badge, model and helpers, and its permission prompts.
    apply("opencode");
    run(600);
    ui_open_chat("oc-ses_1");
    run(900);
    shot("08c_chat_opencode");
    expect_text("Plan the change");
    apply("permission_opencode");
    run(1200);
    shot("09b_permission_opencode");
    expect_text("OpenCode wants to run a command");
    tap("Allow once");
    run(500);
    expect_sent("\"id\":\"p9\",\"decision\":\"allow\"");
    expect_text("OpenCode is carrying on.");
    apply("busy");
    run(2500);

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
    {
        lv_obj_t *l = find_label(lv_screen_active(), "Right way up in the case");
        lv_obj_t *sw = l ? lv_obj_get_child(lv_obj_get_parent(l), 1) : NULL;
        if (sw) {
            lv_obj_scroll_to_view_recursive(sw, LV_ANIM_OFF);
            run(100);
            tap_obj(sw, "the turn-around switch");
            run(300);
        }
        printf("%s  the screen can be turned around from Settings\n", s_rotated ? "PASS" : "FAIL");
        if (!s_rotated) s_failures++;
    }
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

    housekeeping_tests();

    {
        // DK_CHAOS=<rounds> for a longer run.
        const char *cr = getenv("DK_CHAOS");
        int rounds = cr ? atoi(cr) : 150;
        chaos(rounds);
        printf("PASS  %d rounds of random snapshots and taps without a crash\n", rounds);
    }
    printf(s_failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", s_failures);
    return s_failures ? 1 : 0;
}
