// Deskling, the character: a soft cream blob with eyes, blush and a mouth,
// built from plain LVGL objects so every part can move.
#pragma once

#include "lvgl.h"

typedef enum {
    DK_MOOD_CALM,    // idle: slow blink, gentle bob, smile
    DK_MOOD_BUSY,    // an agent is working: bob, glance around, thinking dots
    DK_MOOD_WORRIED, // something needs you: tremble, darting eyes, sweat drop
    DK_MOOD_CURIOUS, // a question: head tilt, "?" bubble
    DK_MOOD_HAPPY,   // done / answered: squash-and-stretch jumps, sparkles
    DK_MOOD_SLEEPY,  // night: closed eyes, breathing, floating z's
} dk_mood_t;

/** Create a Deskling `size` px wide (the face sits in a size x size box). */
lv_obj_t *deskling_create(lv_obj_t *parent, int32_t size, lv_color_t body);
void deskling_set_mood(lv_obj_t *d, dk_mood_t mood);
dk_mood_t deskling_get_mood(lv_obj_t *d);
/** Play the happy mood for `ms`, then go back to whatever mood was set before. */
void deskling_celebrate(lv_obj_t *d, uint32_t ms);

/** What Deskling wears: DK_OUTFIT_* bits from model.h (one hat at a time: crown, party hat, then
    headphones). Cheap: the pieces are made once and shown or hidden. */
void deskling_set_outfit(lv_obj_t *d, uint32_t outfit);
