// Host check of the device's picture path (net/images.c): PNG decode with LVGL's lodepng and
// the one-off shrink, on a real picture.  build-sim/imgtest <file.png>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#define UI_IMAGE_MAX_W 520
#define UI_IMAGE_MAX_H 360
#define ESP_LOGW(t, ...) fprintf(stderr, __VA_ARGS__)
static const char *TAG = "imgtest";
static void *ps_malloc(size_t n) { return malloc(n); }
unsigned lodepng_decode32(unsigned char **out, unsigned *w, unsigned *h, const unsigned char *in, size_t insize);
#include "images_core.inc"

int main(int argc, char **argv)
{
    lv_init();
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    uint8_t *d = malloc(n);
    if (fread(d, 1, n, f) != (size_t)n) return 2;
    int w = 0, h = 0;
    uint16_t *px = decode_png(d, n, &w, &h);
    printf("%s -> %dx%d %s\n", argv[1], w, h, px ? "ok" : "FAILED");
    if (px && argc > 2) {
        FILE *o = fopen(argv[2], "wb");
        fwrite(px, 2, (size_t)w * h, o);
        fclose(o);
    }
    free(px);
    return px ? 0 : 1;
}
