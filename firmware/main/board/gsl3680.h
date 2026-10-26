// GSL3680 touch controller driver with the startup fixes the stock drivers lack.
#pragma once


#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"

#define GSL3680_I2C_ADDRESS 0x40

/**
 * Create the touch handle: resets the controller, uploads its RAM firmware and
 * waits for the running marker, retrying the whole sequence once.
 */
esp_err_t dk_gsl3680_new(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out);

/** Calibration: raw sensor (a, b) -> landscape (x, y) as x = m0*a + m1*b + m2, y = m3*a + m4*b + m5.
 *  rotation is the LVGL display rotation (90 or 270). m may be NULL to keep the current mapping. */
void dk_gsl3680_set_mapping(const float m[6], int rotation);
/** The latest raw reading of the first finger. Returns true while a finger is down. */
bool dk_gsl3680_raw(int32_t *a, int32_t *b);
