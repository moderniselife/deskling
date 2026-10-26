// Deskling: a cute animated desk screen for your AI agents.
#include <stdlib.h>
#include <time.h>

#include "board.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "net.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "ui.h"

static const char *TAG = "deskling";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    setenv("TZ", CONFIG_DESKLING_TZ, 1);
    tzset();

    board_init();

    lvgl_port_lock(0);
    ui_set_backlight_cb(board_set_backlight);
    ui_init();
    lvgl_port_unlock();
    board_set_backlight(100);
    ESP_LOGI(TAG, "hello from Deskling");

    net_start();
}
