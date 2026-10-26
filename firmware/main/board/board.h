// Guition JC8012P4A1C: 10.1" 800x1280 MIPI-DSI panel (JD9365), GSL3680 touch, LEDC backlight.
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "lvgl.h"

// Pins from the vendor schematic (see docs/hardware.md).
#define BOARD_LCD_RST_GPIO 27
#define BOARD_LCD_BL_GPIO 23
#define BOARD_TOUCH_SDA_GPIO 7
#define BOARD_TOUCH_SCL_GPIO 8
#define BOARD_TOUCH_RST_GPIO 22
#define BOARD_TOUCH_INT_GPIO 21
#define BOARD_MIPI_PHY_LDO_CHAN 3
#define BOARD_MIPI_PHY_LDO_MV 2500

#define BOARD_LCD_H_RES 800
#define BOARD_LCD_V_RES 1280

/** Bring up the panel, backlight, touch and LVGL. Returns the (landscape) display. */
/** rotate_180: the picture turned around, which is right way up in the Deskling case. */
lv_display_t *board_init(bool rotate_180);
/** Turn the picture (and touch) around now. */
void board_set_rotate_180(bool on);
/** 0..100 */
void board_set_backlight(uint8_t percent);
/** The I2C bus the touch controller and audio codec share (NULL if it failed). */
i2c_master_bus_handle_t board_i2c_bus(void);

typedef struct {
    uint32_t frames;  // redraws in the last 5 s that had something to draw
    float avg_ms;     // average time per redraw (render + rotate + copy)
    float worst_ms;
    uint8_t busy_pct; // share of those 5 s spent redrawing
    float draw_ms;    // of avg_ms: LVGL drawing
    float flush_ms;   //            PPA rotation + handing bands over
    float wait_ms;    //            waiting for the DMA2D copy into the framebuffer
    uint32_t kpx;     // thousands of pixels redrawn per redraw
    bool internal_bands;
    uint16_t band_lines;
} board_display_stats_t;
/** Redraw timings from the last 5-second window with any drawing. */
board_display_stats_t board_display_stats(void);
