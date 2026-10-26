// Pictures in the session chat. The device fetches and shrinks them (g_platform->fetch_image);
// this keeps the last few ready to draw and asks the chat to redraw when one arrives.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_internal.h"

#define SLOTS 8

typedef struct {
    char id[24];
    bool pending, failed;
    lv_image_dsc_t dsc;
    uint16_t *px;
    uint32_t used;
} slot_t;

static slot_t s_slots[SLOTS];
static uint32_t s_tick;

static slot_t *find(const char *id)
{
    for (int i = 0; i < SLOTS; i++)
        if (s_slots[i].id[0] && strcmp(s_slots[i].id, id) == 0) return &s_slots[i];
    return NULL;
}

static slot_t *take_slot(void)
{
    slot_t *best = &s_slots[0];
    for (int i = 0; i < SLOTS; i++) {
        if (!s_slots[i].id[0]) return &s_slots[i];
        if (!s_slots[i].pending && s_slots[i].used < best->used) best = &s_slots[i];
    }
    free(best->px);
    memset(best, 0, sizeof(*best));
    return best;
}

const lv_image_dsc_t *ui_image_get(const char *id, bool *failed)
{
    if (failed) *failed = false;
    if (!id || !id[0]) return NULL;
    slot_t *s = find(id);
    if (s) {
        s->used = ++s_tick;
        if (failed) *failed = s->failed;
        return s->px ? &s->dsc : NULL;
    }
    if (!g_platform || !g_platform->fetch_image) {
        if (failed) *failed = true;
        return NULL;
    }
    s = take_slot();
    snprintf(s->id, sizeof(s->id), "%s", id);
    s->pending = true;
    s->used = ++s_tick;
    g_platform->fetch_image(id);
    return NULL;
}

int ui_image_state(const char *id)
{
    slot_t *s = id && id[0] ? find(id) : NULL;
    return !s ? 0 : s->px ? 1 : s->failed ? 2 : 0;
}

void ui_image_ready(const char *id, const uint16_t *rgb565, int w, int h)
{
    slot_t *s = find(id);
    if (!s) return; // pushed out while it downloaded
    s->pending = false;
    if (!rgb565 || w <= 0 || h <= 0) {
        s->failed = true;
    } else {
        size_t n = (size_t)w * h * 2;
        s->px = malloc(n);
        if (!s->px) {
            s->failed = true;
        } else {
            memcpy(s->px, rgb565, n);
            memset(&s->dsc, 0, sizeof(s->dsc));
            s->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
            s->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
            s->dsc.header.w = w;
            s->dsc.header.h = h;
            s->dsc.header.stride = w * 2;
            s->dsc.data = (const uint8_t *)s->px;
            s->dsc.data_size = n;
        }
    }
    scr_chat_images_changed();
}
