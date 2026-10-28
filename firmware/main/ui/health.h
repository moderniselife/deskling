// Keeping the board quick over days of running: animations on screens you can't see are
// paused, a chat you've left is let go of after a while, and what the board holds is watched.
// When something has built up that can't be freed in place (memory gone into small pieces, the
// screen getting slower), the board restarts itself quietly while nobody's looking, and once a
// night after a long run anyway, so every morning starts fresh.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui.h"

#define HEALTH_HISTORY 48 // half-hour points: the last day

typedef struct {
    uint32_t at_s;      // uptime
    uint8_t busy;       // % of the time spent drawing, averaged over the half hour
    uint16_t internal_free_kb, internal_largest_kb;
    uint32_t psram_free_kb, psram_largest_kb;
    uint16_t anims, paused, timers;
    uint32_t objects;
} health_point_t;

typedef struct ui_health_s {
    uint32_t uptime_s;
    uint8_t busy;            // the last minute
    uint8_t busy_night;      // the night clock, the last 10 calm minutes (0 = not measured yet)
    uint8_t busy_night_base; // the same, the quickest 10 minutes since starting
    uint32_t anims, paused, timers, objects;
    ui_memory_t mem;
    uint32_t chats_released, pictures_released;
    char problem[80]; // what's wrong, "" if nothing; a fresh start follows when nobody's looking
    int history_n;
    health_point_t history[HEALTH_HISTORY]; // oldest first
} ui_health_t;

/* What the fresh-start decision looks at. Pure, so the simulator can test it. */
typedef struct {
    uint32_t uptime_s;
    int hour;          // local time, -1 if the clock isn't set
    uint32_t idle_s;   // since the screen was last touched
    bool calm;         // home, sessions or night clock, no question waiting, no update or celebration
    ui_memory_t mem;   // zeros = unknown
    uint32_t objects, anims, timers;
    uint8_t busy_night, busy_night_base;
    int slow_windows;  // how many 10-minute windows in a row the night clock was slow
} health_in_t;

typedef struct {
    const char *problem; // NULL: all good
    bool restart_now;    // and it's a good moment to start fresh
} health_verdict_t;

health_verdict_t health_verdict(const health_in_t *in);

void health_init(void);
/* The screen on show changed: pause what's hidden, wake what's shown. */
void health_follow_screen(void);
/* Count of everything on every screen. */
uint32_t health_count_objects(void);
