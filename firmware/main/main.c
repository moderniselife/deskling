// Deskling: a cute animated desk screen for your AI agents.
#include "audio.h"
#include "board.h"
#include "gsl3680.h"

#ifdef CONFIG_DESKLING_ROTATE_180
#define DK_ROTATION 270
#else
#define DK_ROTATION 90
#endif
#include "device.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "images.h"
#include "esp_system.h"
#include "net.h"
#include "nvs_flash.h"
#include "ota.h"
#include "settings.h"
#include "ui.h"

static const char *TAG = "deskling";

void device_set_brightness(uint8_t percent)
{
    settings_set_brightness(percent);
    board_set_backlight(100);
}

static void restart(void)
{
    esp_restart();
}

static bool touch_calibrated(void)
{
    float m[6];
    return settings_get_touch_cal(m);
}

static void touch_save_calibration(const float m[6])
{
    settings_set_touch_cal(m);
    dk_gsl3680_set_mapping(m, DK_ROTATION);
}

void device_set_sound(uint8_t volume, bool quiet_night)
{
    settings_set_sound(volume, quiet_night);
    audio_set_volume(volume);
    net_push_info();
}

// What the UI can ask the device to do. Each call just queues work and returns.
static const ui_platform_t s_platform = {
    .wifi_scan = net_request_scan,
    .wifi_connect = net_request_wifi,
    .relay_set = net_request_relay,
    .check_update = ota_request_check,
    .start_update = ota_request_install,
    .set_brightness = device_set_brightness,
    .restart = restart,
    .play_sound = audio_play,
    .set_sound = device_set_sound,
    .touch_raw = dk_gsl3680_raw,
    .touch_calibrated = touch_calibrated,
    .touch_save_calibration = touch_save_calibration,
    .fetch_image = images_fetch,
};

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    settings_init();
    ota_init();

    board_init();
    float cal[6];
    if (settings_get_touch_cal(cal)) dk_gsl3680_set_mapping(cal, DK_ROTATION);
    audio_init(board_i2c_bus());
    audio_set_volume(settings()->volume);

    lvgl_port_lock(0);
    ui_set_backlight_cb(board_set_backlight);
    ui_set_platform(&s_platform);
    ui_init();
    lvgl_port_unlock();
    board_set_backlight(100);
    ESP_LOGI(TAG, "hello from Deskling %s", ota_variant());

    net_start();
}
