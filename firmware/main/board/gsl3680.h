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
