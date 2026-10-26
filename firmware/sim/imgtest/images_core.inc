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

