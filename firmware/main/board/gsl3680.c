// SPDX-License-Identifier: MIT
//
// GSL3680 touch driver for the Guition JC8012P4A1C.
//
// Based on the MIT-licensed rewrite in guition-jc/esp_lcd_touch_gsl3680 (coordinate
// scaling, packet layout), with the startup fixes from jtenniswood/espcontrol
// commit 18a2163 for boards whose touch fails with "gsl3680 read error" /
// "i2c transaction failed":
//   - one hardware reset before configuring, never another one after it
//   - clear-registers writes the touch-count register 0x80 = 3 (not 0x88 = 1)
//   - the RAM page selector 0xf0 is written as 4 bytes, not 1
//   - poll the 0x5a5a5a5a running marker up to 10 times, and retry the init once
//
// The controller firmware table (GSLX680_FW) is uploaded on every boot. It comes
// from the guition-jc component at build time; see main/CMakeLists.txt.
#include "gsl3680.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gsl3680_firmware.h"

static const char *TAG = "gsl3680";

#define DATA_REG 0x80
#define MAX_CONTACTS 10
#define PACKET_SIZE (4 + 4 * MAX_CONTACTS)
#define MAX_POINTS (CONFIG_ESP_LCD_TOUCH_MAX_POINTS < MAX_CONTACTS ? CONFIG_ESP_LCD_TOUCH_MAX_POINTS : MAX_CONTACTS)


static esp_err_t wr(esp_lcd_touch_handle_t tp, uint8_t reg, const uint8_t *buf, size_t len)
{
    return esp_lcd_panel_io_tx_param(tp->io, reg, buf, len);
}

static esp_err_t wr1(esp_lcd_touch_handle_t tp, uint8_t reg, uint8_t v)
{
    return wr(tp, reg, &v, 1);
}

static void hw_reset(esp_lcd_touch_handle_t tp)
{
    if (tp->config.rst_gpio_num == GPIO_NUM_NC) return;
    gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(tp->config.rst_gpio_num, !tp->config.levels.reset);
    vTaskDelay(pdMS_TO_TICKS(20));
}

static esp_err_t core_reset(esp_lcd_touch_handle_t tp)
{
    static const uint8_t zero[4] = {0};
    ESP_RETURN_ON_ERROR(wr1(tp, 0xe0, 0x88), TAG, "core reset");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(wr1(tp, 0xe4, 0x04), TAG, "clock");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(wr(tp, 0xbc, zero, sizeof(zero)), TAG, "power");
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

static esp_err_t start_core(esp_lcd_touch_handle_t tp)
{
    ESP_RETURN_ON_ERROR(wr1(tp, 0xe0, 0x00), TAG, "start");
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

static esp_err_t running_marker(esp_lcd_touch_handle_t tp)
{
    uint8_t st[4] = {0};
    for (int i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(30));
        if (esp_lcd_panel_io_rx_param(tp->io, 0xb0, st, sizeof(st)) == ESP_OK && st[0] == 0x5a && st[1] == 0x5a && st[2] == 0x5a &&
            st[3] == 0x5a) {
            return ESP_OK;
        }
    }
    ESP_LOGE(TAG, "running marker missing (got %02x %02x %02x %02x)", st[0], st[1], st[2], st[3]);
    return ESP_FAIL;
}

static esp_err_t init_once(esp_lcd_touch_handle_t tp)
{
    hw_reset(tp);
    // Scan-core setup: reset command, touch count 3, clock, release.
    ESP_RETURN_ON_ERROR(wr1(tp, 0xe0, 0x88), TAG, "clear: reset");
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(wr1(tp, 0x80, 3), TAG, "clear: touch count");
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(wr1(tp, 0xe4, 0x04), TAG, "clear: clock");
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(wr1(tp, 0xe0, 0x00), TAG, "clear: release");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(core_reset(tp), TAG, "core reset");

    for (size_t i = 0; i < sizeof(GSLX680_FW) / sizeof(GSLX680_FW[0]); i++) {
        uint32_t v = GSLX680_FW[i].val;
        uint8_t b[4] = {v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff};
        // Every write is 4 bytes, the 0xf0 page selector included.
        ESP_RETURN_ON_ERROR(wr(tp, (uint8_t)GSLX680_FW[i].offset, b, 4), TAG, "firmware word %u", (unsigned)i);
    }
    ESP_RETURN_ON_ERROR(start_core(tp), TAG, "start");
    ESP_RETURN_ON_ERROR(core_reset(tp), TAG, "core reset 2");
    ESP_RETURN_ON_ERROR(start_core(tp), TAG, "start 2");
    return running_marker(tp);
}

static esp_err_t init_with_retry(esp_lcd_touch_handle_t tp)
{
    esp_err_t err = init_once(tp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch startup failed (%s), retrying once", esp_err_to_name(err));
        err = init_once(tp);
    }
    return err;
}

// ---------------------------------------------------------------- mapping
//
// The controller reports two raw 12-bit axes in its own sensor units. A calibration maps them
// to the landscape screen the UI sees (an affine fit from five taps, so it covers any rotation,
// flip, scale and offset), and the result is turned back into panel coordinates, because LVGL
// applies the display rotation to touch points itself.

static portMUX_TYPE s_map_lock = portMUX_INITIALIZER_UNLOCKED;
// landscape_x = m[0]*a + m[1]*b + m[2]; landscape_y = m[3]*a + m[4]*b + m[5], for the picture at
// 90 (the calibration is kept that way whichever way up the picture is).
// The default is a rough guess from the bundled controller firmware; calibration replaces it.
static float s_map[6] = {0.0f, -0.769f, 1279.0f, 0.893f, 0.0f, 0.0f};
static int s_rotation = 90; // the LVGL display rotation: 90 or 270
static volatile int32_t s_raw_a, s_raw_b;
static volatile bool s_raw_down;

void dk_gsl3680_set_rotation(int rotation)
{
    portENTER_CRITICAL(&s_map_lock);
    s_rotation = rotation;
    portEXIT_CRITICAL(&s_map_lock);
}

void dk_gsl3680_set_mapping(const float m[6], int rotation)
{
    portENTER_CRITICAL(&s_map_lock);
    if (m) memcpy(s_map, m, sizeof(s_map));
    s_rotation = rotation;
    portEXIT_CRITICAL(&s_map_lock);
}

bool dk_gsl3680_raw(int32_t *a, int32_t *b)
{
    *a = s_raw_a;
    *b = s_raw_b;
    return s_raw_down;
}

static void to_panel(esp_lcd_touch_handle_t tp, int32_t a, int32_t b, uint16_t *px, uint16_t *py)
{
    float m[6];
    int rot;
    portENTER_CRITICAL(&s_map_lock);
    memcpy(m, s_map, sizeof(m));
    rot = s_rotation;
    portEXIT_CRITICAL(&s_map_lock);
    const int32_t lw = tp->config.y_max, lh = tp->config.x_max; // landscape is the panel turned sideways
    int32_t lx = (int32_t)(m[0] * a + m[1] * b + m[2] + 0.5f);
    int32_t ly = (int32_t)(m[3] * a + m[4] * b + m[5] + 0.5f);
    lx = lx < 0 ? 0 : lx >= lw ? lw - 1 : lx;
    ly = ly < 0 ? 0 : ly >= lh ? lh - 1 : ly;
    // The calibration is for the picture at 90. Turned around (270), the same spot on the glass
    // is the opposite corner of the screen.
    if (rot == 270) {
        lx = lw - 1 - lx;
        ly = lh - 1 - ly;
    }
    // Inverse of lv_display_rotate_point(): 90 maps panel (x,y) to (H-1-y, x).
    if (rot == 270) {
        *px = (uint16_t)(lh - 1 - ly);
        *py = (uint16_t)lx;
    } else {
        *px = (uint16_t)ly;
        *py = (uint16_t)(lw - 1 - lx);
    }
}

static esp_err_t read_data(esp_lcd_touch_handle_t tp)
{
    // This runs inside LVGL's task every refresh and blocks it for the whole I2C transfer, so
    // read just the header and first finger (8 bytes, ~0.2 ms) and fetch the full packet only
    // when more than one finger is down, instead of all 44 bytes (~1.1 ms) every time.
    uint8_t pkt[PACKET_SIZE] = {0};
    esp_err_t err = esp_lcd_panel_io_rx_param(tp->io, DATA_REG, pkt, 8);
    if (err == ESP_OK && pkt[0] > 1 && pkt[0] <= MAX_CONTACTS) err = esp_lcd_panel_io_rx_param(tp->io, DATA_REG, pkt, 4 + 4 * pkt[0]);
    uint8_t n = 0;
    esp_lcd_touch_point_data_t pts[MAX_POINTS] = {0};
    if (err == ESP_OK && pkt[0] <= MAX_CONTACTS) {
        for (uint8_t i = 0; i < pkt[0] && n < MAX_POINTS; i++) {
            const uint8_t *p = pkt + 4 + 4 * i;
            // The high nibbles carry a finger id on some frames; the coordinates are the low 12 bits.
            int32_t a = p[0] | ((p[1] & 0x0f) << 8);
            int32_t b = p[2] | ((p[3] & 0x0f) << 8);
            if (n == 0) {
                s_raw_a = a;
                s_raw_b = b;
            }
            to_panel(tp, a, b, &pts[n].x, &pts[n].y);
            pts[n].track_id = i + 1;
            n++;
        }
    }
    s_raw_down = n > 0;
    // Log where each new press lands (useful when checking a calibration from the boot log).
    static bool was_down;
    if (n && !was_down) ESP_LOGI(TAG, "press raw %ld,%ld -> panel %u,%u", (long)s_raw_a, (long)s_raw_b, pts[0].x, pts[0].y);
    was_down = n > 0;

    portENTER_CRITICAL(&tp->data.lock);
    // Light smoothing for a held finger; new touches report immediately.
    if (n && tp->data.points) {
        pts[0].x = (3U * pts[0].x + tp->data.coords[0].x + 2) / 4;
        pts[0].y = (3U * pts[0].y + tp->data.coords[0].y + 2) / 4;
    }
    memcpy(tp->data.coords, pts, sizeof(pts[0]) * n);
    tp->data.points = n;
    portEXIT_CRITICAL(&tp->data.lock);
    return err;
}

static bool get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    portENTER_CRITICAL(&tp->data.lock);
    *point_num = tp->data.points < max_point_num ? tp->data.points : max_point_num;
    for (uint8_t i = 0; i < *point_num; i++) {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;
        if (strength) strength[i] = 0;
    }
    portEXIT_CRITICAL(&tp->data.lock);
    return *point_num > 0;
}

static esp_err_t get_track_id(esp_lcd_touch_handle_t tp, uint8_t *track_id, uint8_t point_num)
{
    portENTER_CRITICAL(&tp->data.lock);
    for (uint8_t i = 0; i < point_num && i < tp->data.points; i++) track_id[i] = tp->data.coords[i].track_id;
    portEXIT_CRITICAL(&tp->data.lock);
    return ESP_OK;
}

static esp_err_t del(esp_lcd_touch_handle_t tp)
{
    if (tp->config.int_gpio_num != GPIO_NUM_NC) gpio_reset_pin(tp->config.int_gpio_num);
    if (tp->config.rst_gpio_num != GPIO_NUM_NC) gpio_reset_pin(tp->config.rst_gpio_num);
    free(tp);
    return ESP_OK;
}

static esp_err_t enter_sleep(esp_lcd_touch_handle_t tp)
{
    if (tp->config.rst_gpio_num == GPIO_NUM_NC) return ESP_ERR_NOT_SUPPORTED;
    return gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset);
}

static esp_err_t exit_sleep(esp_lcd_touch_handle_t tp)
{
    return init_with_retry(tp); // the RAM firmware is lost while held in reset
}

esp_err_t dk_gsl3680_new(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out)
{
    ESP_RETURN_ON_FALSE(io && config && out, ESP_ERR_INVALID_ARG, TAG, "bad args");
    esp_lcd_touch_handle_t tp = calloc(1, sizeof(esp_lcd_touch_t));
    ESP_RETURN_ON_FALSE(tp, ESP_ERR_NO_MEM, TAG, "no memory");
    tp->io = io;
    tp->config = *config;
    tp->data.lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    tp->read_data = read_data;
    tp->get_xy = get_xy;
    tp->get_track_id = get_track_id;
    tp->del = del;
    tp->enter_sleep = enter_sleep;
    tp->exit_sleep = exit_sleep;

    if (config->rst_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t rst = {.mode = GPIO_MODE_OUTPUT, .pin_bit_mask = BIT64(config->rst_gpio_num)};
        gpio_config(&rst);
    }
    if (config->int_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t in = {.mode = GPIO_MODE_INPUT, .pin_bit_mask = BIT64(config->int_gpio_num)};
        gpio_config(&in);
    }

    esp_err_t err = init_with_retry(tp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch controller didn't start: the screen still works, touch won't. Power-cycle to retry.");
        del(tp);
        return err;
    }
    ESP_LOGI(TAG, "touch ready");
    *out = tp;
    return ESP_OK;
}
