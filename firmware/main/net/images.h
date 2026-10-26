#pragma once
#include <stdint.h>

/* Fetch a chat picture from the relay in the background; the UI gets it via ui_image_ready. */
void images_fetch(const char *id);

typedef struct {
    uint32_t requested, done, failed;
    char last[96]; // what happened to the latest one
} images_stats_t;
images_stats_t images_stats(void);
