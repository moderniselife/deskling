// LVGL's memory (CONFIG_LV_USE_CUSTOM_MALLOC): small objects go to internal RAM only while plenty
// is left, everything else to PSRAM.
//
// With plain malloc, anything under 4 KB lands in internal RAM, and a screen of formatted chat
// bubbles is thousands of small objects. Internal RAM is also what Wi-Fi, the network buffers, the
// web server and the deskling.local responder live on, so opening a long chat starved them and
// deskling.local stopped answering. PSRAM has 32 MB; internal RAM has a few hundred KB.
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "lvgl.h"

#define INTERNAL_RESERVE (128 * 1024) // below this much free internal RAM, LVGL uses PSRAM only
#define SMALL 256                     // only tiny, hot objects are worth internal RAM

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void *lv_malloc_core(size_t size)
{
    void *p = NULL;
    if (size <= SMALL && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > INTERNAL_RESERVE) p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_8BIT); // PSRAM full: anything rather than crash
    return p;
}

void *lv_realloc_core(void *p, size_t new_size)
{
    if (!p) return lv_malloc_core(new_size);
    // Keep it where it is if it can grow there; a block in internal RAM moves out when it grows big.
    if (esp_ptr_external_ram(p) || new_size <= SMALL) {
        void *q = heap_caps_realloc(p, new_size, esp_ptr_external_ram(p) ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (q) return q;
    }
    void *q = heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return q ? q : heap_caps_realloc(p, new_size, MALLOC_CAP_8BIT);
}

void lv_free_core(void *p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon)
{
    memset(mon, 0, sizeof(*mon));
    mon->total_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    mon->free_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    mon->free_biggest_size = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    mon->used_pct = mon->total_size ? (uint8_t)(100 - (mon->free_size * 100 / mon->total_size)) : 0;
}

lv_result_t lv_mem_test_core(void)
{
    return heap_caps_check_integrity_all(true) ? LV_RESULT_OK : LV_RESULT_INVALID;
}
