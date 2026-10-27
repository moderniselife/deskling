// See inbox.h. Runs the same on the board and in the simulator (where it's single-threaded).
#include "inbox.h"

#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "ui_internal.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t s_mu;
static void lock(void) { xSemaphoreTake(s_mu, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mu); }
static void lock_init(void)
{
    if (!s_mu) s_mu = xSemaphoreCreateMutex();
}
static void *big_alloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
#else
#include <pthread.h>
static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;
static void lock(void) { pthread_mutex_lock(&s_mu); }
static void unlock(void) { pthread_mutex_unlock(&s_mu); }
static void lock_init(void) {}
static void *big_alloc(size_t n) { return malloc(n); }
#endif

#define DETAILS_MAX 4
#define PICTURES_MAX 8
#define MIN_GAP_MS 200     // draw at most five snapshots a second
#define HOLD_MAX_MS 1500   // while you're touching the screen, wait at most this long

typedef struct {
    char *json;
    size_t len;
} msg_t;

typedef struct {
    char id[24];
    uint16_t *px;
    int w, h;
} pic_t;

static msg_t s_snap;                // the newest snapshot not drawn yet
static uint32_t s_snap_at;          // when it (or the one it replaced) arrived
static bool s_snap_urgent;          // a permission or question: draw it straight away
static msg_t s_details[DETAILS_MAX];
static int s_details_n;
static pic_t s_pics[PICTURES_MAX];
static int s_pics_n;
static uint32_t s_last_apply;
static inbox_stats_t s_stats;
static uint64_t s_apply_total;
static dk_model_t *s_model;

static char *copy_of(const char *s, size_t n)
{
    char *c = big_alloc(n + 1);
    if (!c) return NULL;
    memcpy(c, s, n);
    c[n] = '\0';
    return c;
}

/* The cause is near the start of a snapshot: {"type":"snapshot","now":…,"cause":{"kind":"…"}. */
static bool urgent(const char *json, size_t len)
{
    char head[400];
    size_t n = len < sizeof(head) - 1 ? len : sizeof(head) - 1;
    memcpy(head, json, n);
    head[n] = '\0';
    return strstr(head, "\"kind\":\"permission\"") || strstr(head, "\"kind\":\"question\"") || strstr(head, "\"kind\":\"reminder_due\"") ||
           strstr(head, "\"kind\":\"ping\"");
}

void inbox_push(const char *json, size_t len)
{
    if (!json || !len) return;
    bool detail = len > 16 && strncmp(json, "{\"type\":\"detail\"", 16) == 0;
    // Anything else that isn't a snapshot (a newer relay's message) must not push one out.
    if (!detail && !(len > 18 && strncmp(json, "{\"type\":\"snapshot\"", 18) == 0)) return;
    char *c = copy_of(json, len);
    if (!c) return;
    char *drop = NULL;
    lock();
    if (detail) {
        if (s_details_n == DETAILS_MAX) {
            drop = s_details[0].json; // the oldest answer: you've asked for something newer since
            memmove(&s_details[0], &s_details[1], sizeof(s_details[0]) * (DETAILS_MAX - 1));
            s_details_n--;
        }
        s_details[s_details_n++] = (msg_t){c, len};
    } else {
        bool was_urgent = s_snap.json && s_snap_urgent;
        if (!s_snap.json) s_snap_at = lv_tick_get();
        drop = s_snap.json;
        s_snap = (msg_t){c, len};
        // A newer snapshot holds everything an urgent one did, so keep the hurry.
        s_snap_urgent = was_urgent || urgent(json, len);
        s_stats.received++;
    }
    unlock();
    free(drop);
}

void inbox_image(const char *id, uint16_t *px, int w, int h)
{
    pic_t drop = {0};
    lock();
    if (s_pics_n == PICTURES_MAX) {
        drop = s_pics[0];
        memmove(&s_pics[0], &s_pics[1], sizeof(s_pics[0]) * (PICTURES_MAX - 1));
        s_pics_n--;
    }
    pic_t *p = &s_pics[s_pics_n++];
    memset(p, 0, sizeof(*p));
    strncpy(p->id, id ? id : "", sizeof(p->id) - 1);
    p->px = px;
    p->w = w;
    p->h = h;
    unlock();
    if (drop.id[0]) {
        // Too many at once: the oldest is reported as failed rather than left "loading".
        ui_image_ready(drop.id, NULL, 0, 0);
        free(drop.px);
    }
}

static bool touching(void)
{
    for (lv_indev_t *in = lv_indev_get_next(NULL); in; in = lv_indev_get_next(in)) {
        if (lv_indev_get_type(in) != LV_INDEV_TYPE_POINTER) continue;
        if (lv_indev_get_state(in) == LV_INDEV_STATE_PRESSED || lv_indev_get_scroll_obj(in)) return true;
    }
    return false;
}

void inbox_pump(void)
{
    // Answers you asked for and pictures first: they're small and you're waiting for them.
    msg_t details[DETAILS_MAX];
    pic_t pics[PICTURES_MAX];
    int nd, np;
    lock();
    nd = s_details_n;
    memcpy(details, s_details, sizeof(details[0]) * nd);
    s_details_n = 0;
    np = s_pics_n;
    memcpy(pics, s_pics, sizeof(pics[0]) * np);
    s_pics_n = 0;
    unlock();
    for (int i = 0; i < nd; i++) {
        dk_detail_t d;
        if (dk_detail_parse(details[i].json, details[i].len, &d)) {
            ui_chat_detail(&d);
            s_stats.details++;
        }
        dk_detail_free(&d);
        free(details[i].json);
    }
    for (int i = 0; i < np; i++) {
        ui_image_ready(pics[i].id, pics[i].px, pics[i].w, pics[i].h);
        free(pics[i].px);
        s_stats.pictures++;
    }

    // The newest snapshot, at most five a second, and not under your finger unless it's urgent.
    uint32_t now = lv_tick_get();
    lock();
    bool have = s_snap.json != NULL, hurry = s_snap_urgent;
    uint32_t waited = lv_tick_diff(now, s_snap_at);
    unlock();
    if (!have) return;
    if (!hurry) {
        if (lv_tick_diff(now, s_last_apply) < MIN_GAP_MS) return;
        if (touching() && waited < HOLD_MAX_MS) return;
    }
    lock();
    msg_t m = s_snap;
    s_snap = (msg_t){0};
    s_snap_urgent = false;
    unlock();
    if (waited > s_stats.waiting_ms_max) s_stats.waiting_ms_max = waited;
    if (!s_model) s_model = big_alloc(sizeof(dk_model_t));
    uint32_t t0 = lv_tick_get();
    if (s_model && dk_model_parse(m.json, m.len, s_model)) {
        ui_take_model(&s_model); // s_model is now the previous one, ready for the next snapshot
        s_stats.drawn++;
        uint32_t ms = lv_tick_elaps(t0);
        s_apply_total += ms;
        s_stats.apply_ms_avg = (uint32_t)(s_apply_total / s_stats.drawn);
        if (ms > s_stats.apply_ms_max) s_stats.apply_ms_max = ms;
    }
    free(m.json);
    s_last_apply = lv_tick_get();
}

static void pump_cb(lv_timer_t *t)
{
    (void)t;
    inbox_pump();
}

void inbox_init(void)
{
    static bool started;
    if (started) return;
    started = true;
    lock_init();
    lv_timer_create(pump_cb, 20, NULL);
}

inbox_stats_t inbox_stats(void)
{
    lock();
    inbox_stats_t s = s_stats;
    unlock();
    return s;
}
