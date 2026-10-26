// Chat pictures: fetched from the relay (GET /img/<id>), decoded (JPEG in hardware, PNG with
// LVGL's lodepng), shrunk once to fit the chat, and handed to the UI.
#include "images.h"

#include <stdlib.h>
#include <string.h>

#include "driver/jpeg_decode.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "settings.h"
#include "ui.h"

static const char *TAG = "images";
#define MAX_BYTES (2 * 1024 * 1024)

/* lodepng ships inside LVGL (LV_USE_LODEPNG). LVGL's copy hands back an lv_draw_buf_t through the
   `out` pointer, not raw pixels; its bytes are still lodepng's R,G,B,A (LVGL swaps them later). */
unsigned lodepng_decode32(unsigned char **out, unsigned *w, unsigned *h, const unsigned char *in, size_t insize);

typedef struct {
    char id[24];
} job_t;

static QueueHandle_t s_jobs;
static jpeg_decoder_handle_t s_jpeg;
static images_stats_t s_stats;

static void note(const char *id, const char *what)
{
    snprintf(s_stats.last, sizeof(s_stats.last), "%.12s: %s", id, what);
    ESP_LOGI(TAG, "%s", s_stats.last);
}

images_stats_t images_stats(void) { return s_stats; }

static void *ps_malloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

static uint8_t *download(const char *id, size_t *len)
{
    const dk_settings_t *st = settings();
    if (!st->relay_url[0] || !st->token[0]) return NULL;
    char url[200], auth[120];
    const char *u = st->relay_url;
    size_t ul = strlen(u);
    snprintf(url, sizeof(url), "%.*s/img/%s", (int)(ul && u[ul - 1] == '/' ? ul - 1 : ul), u, id);
    if (strncmp(url, "http", 4) != 0) snprintf(url, sizeof(url), "https://%s/img/%s", u, id);
    snprintf(auth, sizeof(auth), "Bearer %s", st->token);
    esp_http_client_config_t cfg = {.url = url, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 15000, .buffer_size = 4096};
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return NULL;
    esp_http_client_set_header(c, "Authorization", auth);
    uint8_t *buf = NULL;
    if (esp_http_client_open(c, 0) == ESP_OK) {
        int64_t n = esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        if (status == 200 && n > 0 && n <= MAX_BYTES && (buf = ps_malloc((size_t)n))) {
            size_t got = 0;
            while (got < (size_t)n) {
                int r = esp_http_client_read(c, (char *)buf + got, (int)((size_t)n - got));
                if (r <= 0) break;
                got += (size_t)r;
            }
            if (got != (size_t)n) {
                free(buf);
                buf = NULL;
            } else {
                *len = got;
            }
        } else {
            char why[64];
            snprintf(why, sizeof(why), "HTTP %d, %lld bytes", status, (long long)n);
            note(id, why);
        }
    } else {
        note(id, "couldn't reach the relay");
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return buf;
}

static void fit(int w, int h, int *ow, int *oh)
{
    *ow = w;
    *oh = h;
    if (*ow > UI_IMAGE_MAX_W) {
        *oh = (int)((int64_t)*oh * UI_IMAGE_MAX_W / *ow);
        *ow = UI_IMAGE_MAX_W;
    }
    if (*oh > UI_IMAGE_MAX_H) {
        *ow = (int)((int64_t)*ow * UI_IMAGE_MAX_H / *oh);
        *oh = UI_IMAGE_MAX_H;
    }
    if (*ow < 1) *ow = 1;
    if (*oh < 1) *oh = 1;
}

static inline uint16_t pack(uint32_t r, uint32_t g, uint32_t b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }

/* Box-filter shrink. src is R,G,B,A bytes (argb=true, blended onto white) or RGB565;
   stride_px is the source row length in pixels. */
static uint16_t *shrink(const uint8_t *src, bool argb, int w, int h, int stride_px, int ow, int oh)
{
    uint16_t *dst = ps_malloc((size_t)ow * oh * 2);
    if (!dst) return NULL;
    for (int y = 0; y < oh; y++) {
        int y0 = (int)((int64_t)y * h / oh), y1 = (int)((int64_t)(y + 1) * h / oh);
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < ow; x++) {
            int x0 = (int)((int64_t)x * w / ow), x1 = (int)((int64_t)(x + 1) * w / ow);
            if (x1 <= x0) x1 = x0 + 1;
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int sy = y0; sy < y1; sy++)
                for (int sx = x0; sx < x1; sx++) {
                    if (argb) {
                        const uint8_t *p = src + ((size_t)sy * stride_px + sx) * 4;
                        uint32_t a = p[3];
                        r += (p[0] * a + 255 * (255 - a)) / 255;
                        g += (p[1] * a + 255 * (255 - a)) / 255;
                        b += (p[2] * a + 255 * (255 - a)) / 255;
                    } else {
                        uint16_t v = ((const uint16_t *)src)[(size_t)sy * stride_px + sx];
                        r += ((v >> 11) & 0x1F) << 3;
                        g += ((v >> 5) & 0x3F) << 2;
                        b += (v & 0x1F) << 3;
                    }
                    n++;
                }
            dst[y * ow + x] = pack(r / n, g / n, b / n);
        }
    }
    return dst;
}

static uint16_t *decode_png(const uint8_t *data, size_t len, int *ow, int *oh)
{
    unsigned char *res = NULL;
    unsigned w = 0, h = 0;
    if (lodepng_decode32(&res, &w, &h, data, len) || !res) {
        ESP_LOGW(TAG, "png decode failed");
        return NULL;
    }
    lv_draw_buf_t *buf = (lv_draw_buf_t *)res;
    uint16_t *out = NULL;
    if (buf->data && buf->header.w == w && buf->header.h == h && buf->header.stride >= w * 4) {
        fit((int)w, (int)h, ow, oh);
        out = shrink(buf->data, true, (int)w, (int)h, (int)(buf->header.stride / 4), *ow, *oh);
    }
    lv_draw_buf_destroy(buf);
    return out;
}

static uint16_t *decode_jpeg(const uint8_t *data, size_t len, int *ow, int *oh)
{
    jpeg_decode_picture_info_t info;
    if (jpeg_decoder_get_info(data, len, &info) != ESP_OK || !info.width || !info.height) return NULL;
    if (!s_jpeg) {
        jpeg_decode_engine_cfg_t ecfg = {.intr_priority = 0, .timeout_ms = 2000};
        if (jpeg_new_decoder_engine(&ecfg, &s_jpeg) != ESP_OK) return NULL;
    }
    // Blocks of 16 pixels: the output can be padded out to them.
    int pw = (int)((info.width + 15) & ~15u), ph = (int)((info.height + 15) & ~15u);
    jpeg_decode_memory_alloc_cfg_t in_cfg = {.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER};
    jpeg_decode_memory_alloc_cfg_t out_cfg = {.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
    size_t in_cap = 0, out_cap = 0;
    uint8_t *in = jpeg_alloc_decoder_mem(len, &in_cfg, &in_cap);
    uint8_t *out = jpeg_alloc_decoder_mem((size_t)pw * ph * 2, &out_cfg, &out_cap);
    uint16_t *res = NULL;
    if (in && out) {
        memcpy(in, data, len);
        jpeg_decode_cfg_t dcfg = {.output_format = JPEG_DECODE_OUT_FORMAT_RGB565, .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR, .conv_std = JPEG_YUV_RGB_CONV_STD_BT601};
        uint32_t got = 0;
        if (jpeg_decoder_process(s_jpeg, &dcfg, in, len, out, out_cap, &got) == ESP_OK) {
            fit((int)info.width, (int)info.height, ow, oh);
            res = shrink(out, false, (int)info.width, (int)info.height, pw, *ow, *oh);
        } else {
            ESP_LOGW(TAG, "jpeg decode failed");
        }
    }
    free(in);
    free(out);
    return res;
}

static void worker(void *arg)
{
    (void)arg;
    job_t job;
    for (;;) {
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE) continue;
        size_t len = 0;
        uint8_t *data = download(job.id, &len);
        uint16_t *px = NULL;
        int w = 0, h = 0;
        if (data && len > 8) {
            if (data[0] == 0x89 && data[1] == 'P') px = decode_png(data, len, &w, &h);
            else if (data[0] == 0xFF && data[1] == 0xD8) px = decode_jpeg(data, len, &w, &h);
        }
        free(data);
        if (px) {
            s_stats.done++;
            char ok[48];
            snprintf(ok, sizeof(ok), "shown at %dx%d (%u bytes)", w, h, (unsigned)len);
            note(job.id, ok);
        } else {
            s_stats.failed++;
            if (data == NULL && !s_stats.last[0]) note(job.id, "download failed");
            else if (len > 8) note(job.id, "couldn't decode it");
        }
        // Hand it to the UI; keep trying for the lock rather than leaving it "loading" forever.
        for (int tries = 0; tries < 20; tries++) {
            if (lvgl_port_lock(1000)) {
                ui_image_ready(job.id, px, w, h);
                lvgl_port_unlock();
                break;
            }
        }
        free(px);
    }
}

static void fail_later(lv_timer_t *t)
{
    char *id = lv_timer_get_user_data(t);
    ui_image_ready(id, NULL, 0, 0);
    free(id);
}

static bool s_started;

static void start_worker(void)
{
    if (s_started) return;
    s_jobs = xQueueCreate(16, sizeof(job_t));
    // TLS plus PNG inflate want a roomy stack; internal RAM is tight, so it lives in PSRAM.
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(worker, "images", 24 * 1024, NULL, 3, NULL, tskNO_AFFINITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) ok = xTaskCreate(worker, "images", 16 * 1024, NULL, 3, NULL);
    s_started = ok == pdPASS && s_jobs;
    if (!s_started) note("worker", "couldn't start (out of memory)");
}

void images_fetch(const char *id)
{
    start_worker();
    s_stats.requested++;
    job_t j = {0};
    snprintf(j.id, sizeof(j.id), "%s", id);
    // Called from the UI with the LVGL lock held, so never block here.
    if (!s_started || xQueueSend(s_jobs, &j, 0) != pdTRUE) {
        s_stats.failed++;
        note(id, s_started ? "too many pictures at once" : "picture loader isn't running");
        // Say so on the next UI tick (we're inside the chat's redraw right now).
        char *copy = strdup(j.id);
        lv_timer_t *t = copy ? lv_timer_create(fail_later, 0, copy) : NULL;
        if (t) lv_timer_set_repeat_count(t, 1);
        else free(copy);
    }
}
