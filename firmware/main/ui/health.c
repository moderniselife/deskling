// See health.h. Runs the same on the board and in the simulator.
#include "health.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "src/core/lv_global.h"
#include "src/misc/lv_anim_private.h"
#include "ui_internal.h"

uint32_t lv_timer_get_idle(void); // in lv_timer.c, not in a public header

#define SAMPLE_MS 5000
#define WINDOW_SAMPLES 120               // ten minutes of samples
#define CHAT_RELEASE_S (10 * 60)         // a chat you left this long ago is let go of
#define POINT_S (30 * 60)

static ui_health_t s_h;
static uint32_t s_ticks;                  // samples taken
static uint32_t s_min_sum, s_min_n;       // busy, this minute
static uint32_t s_half_sum, s_half_n;     // busy minutes, this half hour
static uint32_t s_night_sum, s_night_n;   // busy on the calm night clock, this window
static int s_slow_windows;
static uint32_t s_chat_hidden_since;      // uptime (s) the chat was last on show
static bool s_fresh_asked;

// ------------------------------------------------------------ verdict

health_verdict_t health_verdict(const health_in_t *in)
{
    health_verdict_t v = {0};
    const ui_memory_t *m = &in->mem;
    uint32_t wait_s = 0, min_up_s = 0; // how long untouched before restarting, and not before this uptime
    if ((m->internal_largest_kb && m->internal_largest_kb < 12) || (m->internal_free_kb && m->internal_free_kb < 32)) {
        v.problem = "Internal memory is nearly gone or in pieces";
        wait_s = 60, min_up_s = 10 * 60;
    } else if (m->psram_largest_kb && m->psram_largest_kb < 1024) {
        v.problem = "Picture memory is nearly gone or in pieces";
        wait_s = 60, min_up_s = 10 * 60;
    } else if (in->objects > 12000 || in->anims > 600 || in->timers > 100) {
        v.problem = "Too much has built up on the screens";
        wait_s = 120, min_up_s = 30 * 60;
    } else if (in->slow_windows >= 3) {
        v.problem = "The screen has become slow";
        wait_s = 5 * 60, min_up_s = 2 * 3600;
    } else if (in->uptime_s > 20 * 3600 && in->hour >= 3 && in->hour < 5) {
        v.problem = "Due its nightly fresh start";
        wait_s = 30 * 60, min_up_s = 20 * 3600;
    }
    v.restart_now = v.problem && in->calm && in->idle_s >= wait_s && in->uptime_s >= min_up_s;
    return v;
}

// ------------------------------------------------- hidden-screen motion

typedef struct {
    const void *p;
    bool shown;
} ent_t;

static ent_t *s_ents;
static uint32_t s_ents_n, s_ents_cap;

static void collect(lv_obj_t *o, bool shown)
{
    if (s_ents_n == s_ents_cap) {
        uint32_t cap = s_ents_cap ? s_ents_cap * 2 : 1024;
        ent_t *e = lv_realloc(s_ents, cap * sizeof(ent_t));
        if (!e) return;
        s_ents = e, s_ents_cap = cap;
    }
    s_ents[s_ents_n++] = (ent_t){o, shown};
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) collect(lv_obj_get_child(o, i), shown);
}

static int by_ptr(const void *a, const void *b)
{
    const void *x = ((const ent_t *)a)->p, *y = ((const ent_t *)b)->p;
    return x < y ? -1 : x > y;
}

static uint32_t count_tree(lv_obj_t *o)
{
    uint32_t n = 1, c = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < c; i++) n += count_tree(lv_obj_get_child(o, i));
    return n;
}

uint32_t health_count_objects(void)
{
    uint32_t n = count_tree(lv_layer_top()) + count_tree(lv_layer_bottom());
    for (int i = 0; i < SCR_COUNT; i++)
        if (ui_screen(i)) n += count_tree(ui_screen(i));
    return n;
}

/*
 * Every Deskling, glow and spinner keeps moving on screens nobody can see: each step still sets
 * its styles and works out what to redraw. Looping animations on a hidden screen are paused
 * (they pick up where they were when the screen is back); one-off ones finish as usual.
 * Animations on anything that isn't on one of our screens (the top layer, a celebration) are
 * left alone.
 */
void health_follow_screen(void)
{
    lv_obj_t *active = lv_screen_active();
#ifndef ESP_PLATFORM
    if (getenv("DK_NOPAUSE")) return; // the simulator, measuring what pausing saves
#endif
    s_ents_n = 0;
    for (int i = 0; i < SCR_COUNT; i++)
        if (ui_screen(i)) collect(ui_screen(i), ui_screen(i) == active);
    if (!s_ents_n) return;
    qsort(s_ents, s_ents_n, sizeof(ent_t), by_ptr);
    lv_ll_t *ll = &LV_GLOBAL_DEFAULT()->anim_state.anim_ll;
    uint32_t anims = 0, paused = 0;
    for (lv_anim_t *a = lv_ll_get_head(ll); a; a = lv_ll_get_next(ll, a)) {
        anims++;
        ent_t key = {a->var, false};
        const ent_t *e = a->var ? bsearch(&key, s_ents, s_ents_n, sizeof(ent_t), by_ptr) : NULL;
        bool hide = e && !e->shown && a->repeat_cnt == LV_ANIM_REPEAT_INFINITE;
        if (hide && !a->is_paused) lv_anim_pause(a);
        else if (!hide && a->is_paused && a->pause_duration == LV_ANIM_PAUSE_FOREVER) lv_anim_resume(a);
        if (hide) paused++;
    }
    s_h.anims = anims;
    s_h.paused = paused;
}

// ------------------------------------------------------------ sampling

static uint32_t uptime_s(void)
{
    return lv_tick_get() / 1000;
}

static int local_hour(void)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    return tm.tm_year + 1900 < 2024 ? -1 : tm.tm_hour;
}

static bool calm(void)
{
    scr_id_t s = ui_current();
    if (s != SCR_HOME && s != SCR_SESSIONS && s != SCR_NIGHT) return false;
    // A session waiting on you is fine (it's still waiting afterwards); a question on screen isn't.
    if (g_model->pending_n) return false;
    if (g_update_state == UI_UPDATE_DOWNLOADING || g_update_state == UI_UPDATE_DONE) return false;
    return !ui_celebrate_showing();
}

static void minute(void)
{
    uint32_t timers = 0;
    for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)) timers++;
    s_h.timers = timers;
    s_h.objects = health_count_objects();
    memset(&s_h.mem, 0, sizeof(s_h.mem));
    if (g_platform && g_platform->memory) g_platform->memory(&s_h.mem);

    health_in_t in = {
        .uptime_s = s_h.uptime_s,
        .hour = local_hour(),
        .idle_s = lv_display_get_inactive_time(NULL) / 1000,
        .calm = calm(),
        .mem = s_h.mem,
        .objects = s_h.objects,
        .anims = s_h.anims,
        .timers = timers,
        .busy_night = s_h.busy_night,
        .busy_night_base = s_h.busy_night_base,
        .slow_windows = s_slow_windows,
    };
    health_verdict_t v = health_verdict(&in);
    snprintf(s_h.problem, sizeof(s_h.problem), "%s", v.problem ? v.problem : "");
    if (v.restart_now && !s_fresh_asked && g_platform && g_platform->fresh_start) {
        s_fresh_asked = true;
        g_platform->fresh_start(v.problem, ui_current() == SCR_NIGHT ? SCR_NIGHT : SCR_HOME);
    }
}

static void half_hour(void)
{
    health_point_t p = {
        .at_s = s_h.uptime_s,
        .busy = (uint8_t)(s_half_n ? s_half_sum / s_half_n : s_h.busy),
        .internal_free_kb = (uint16_t)s_h.mem.internal_free_kb,
        .internal_largest_kb = (uint16_t)s_h.mem.internal_largest_kb,
        .psram_free_kb = s_h.mem.psram_free_kb,
        .psram_largest_kb = s_h.mem.psram_largest_kb,
        .anims = (uint16_t)s_h.anims,
        .paused = (uint16_t)s_h.paused,
        .timers = (uint16_t)s_h.timers,
        .objects = s_h.objects,
    };
    s_half_sum = s_half_n = 0;
    if (s_h.history_n == HEALTH_HISTORY) {
        memmove(&s_h.history[0], &s_h.history[1], sizeof(s_h.history[0]) * (HEALTH_HISTORY - 1));
        s_h.history_n--;
    }
    s_h.history[s_h.history_n++] = p;
}

static void sample_cb(lv_timer_t *t)
{
    (void)t;
    s_ticks++;
    s_h.uptime_s = uptime_s();
    health_follow_screen(); // anything started on a hidden screen since

    uint32_t busy = 100 - (lv_timer_get_idle() > 100 ? 100 : lv_timer_get_idle());
    s_min_sum += busy, s_min_n++;

    // The night clock is the same picture every night: a fair measure of the board slowing down.
    if (ui_current() == SCR_NIGHT && calm() && lv_display_get_inactive_time(NULL) > 60 * 1000) {
        s_night_sum += busy, s_night_n++;
        if (s_night_n == WINDOW_SAMPLES) {
            uint8_t avg = (uint8_t)(s_night_sum / s_night_n);
            s_h.busy_night = avg;
            if (!s_h.busy_night_base || avg < s_h.busy_night_base) s_h.busy_night_base = avg ? avg : 1;
            bool slow = avg >= 60 && avg > 2 * s_h.busy_night_base + 10;
            s_slow_windows = slow ? s_slow_windows + 1 : 0;
            s_night_sum = s_night_n = 0;
        }
    } else {
        s_night_sum = s_night_n = 0;
    }

    // Let go of a chat you left a while ago: its bubbles, pictures and opened steps.
    if (ui_current() == SCR_CHAT) {
        s_chat_hidden_since = s_h.uptime_s;
    } else if (s_h.uptime_s - s_chat_hidden_since >= CHAT_RELEASE_S && scr_chat_release()) {
        s_h.chats_released++;
        s_h.pictures_released += ui_images_release();
    }

    if (s_ticks % (60000 / SAMPLE_MS) == 0) {
        s_h.busy = (uint8_t)(s_min_n ? s_min_sum / s_min_n : 0);
        s_half_sum += s_h.busy, s_half_n++;
        s_min_sum = s_min_n = 0;
        minute();
    }
    if (s_ticks % (POINT_S * 1000 / SAMPLE_MS) == 0) half_hour();
}

void health_init(void)
{
    static bool started;
    if (started) return;
    started = true;
    lv_timer_create(sample_cb, SAMPLE_MS, NULL);
}

const struct ui_health_s *ui_health(void)
{
    s_h.uptime_s = uptime_s();
    return &s_h;
}
