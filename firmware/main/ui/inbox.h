// What arrives for the screen from other tasks (relay messages, downloaded pictures), handed
// over on the screen's own task. Nothing that receives data ever waits for the screen: a busy
// screen used to hold up the relay connection, so answers and pictures queued behind redraws.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Start handing things over (call once, from the LVGL task or with the LVGL lock held). */
void inbox_init(void);

/** A message from the relay, from any task. Snapshots replace one another (only the newest is
    drawn); "detail" answers are queued and handed over first. */
void inbox_push(const char *json, size_t len);

/** A picture is ready (px may be NULL: it failed). Takes px (malloc'd); any task. */
void inbox_image(const char *id, uint16_t *px, int w, int h);

/** Hand over whatever is waiting now (the timer does this; tests call it directly). */
void inbox_pump(void);

typedef struct {
    uint32_t received;  // snapshots that arrived
    uint32_t drawn;     // ...of which drawn (the rest were replaced by a newer one first)
    uint32_t details;   // "detail" answers handed over
    uint32_t pictures;  // pictures handed over
    uint32_t apply_ms_avg, apply_ms_max; // reading and drawing a snapshot
    uint32_t waiting_ms_max;             // longest a snapshot waited (you were touching the screen)
} inbox_stats_t;
inbox_stats_t inbox_stats(void);
