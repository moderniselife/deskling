// Deskling: a cute animated desk screen for your AI agents.
#include <string.h>

#include "audio.h"
#include "board.h"
#include "gsl3680.h"

#include "device.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "images.h"
#include "crashlog.h"
#include "esp_system.h"
#include "net.h"
#include "nvs_flash.h"
#include "ota.h"
#include "settings.h"
#include "ui.h"

static const char *TAG = "deskling";

void device_set_rotate_180(bool on)
{
    settings_set_rotate_180(on);
    board_set_rotate_180(on);
}

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

static int rotation(void)
{
    return settings()->rotate_180 ? 270 : 90;
}

static void touch_save_calibration(const float m_now[6])
{
    // The five taps map to the screen as it is now. The calibration is kept for the picture at
    // 90, so turned around (270) flip it first: x -> W-1-x, y -> H-1-y.
    float m[6];
    memcpy(m, m_now, sizeof(m));
    if (settings()->rotate_180) {
        const float W = BOARD_LCD_V_RES, H = BOARD_LCD_H_RES; // the landscape screen
        m[0] = -m_now[0], m[1] = -m_now[1], m[2] = (W - 1) - m_now[2];
        m[3] = -m_now[3], m[4] = -m_now[4], m[5] = (H - 1) - m_now[5];
    }
    settings_set_touch_cal(m);
    dk_gsl3680_set_mapping(m, rotation());
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
    .set_rotate_180 = device_set_rotate_180,
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
    crashlog_init();
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    settings_init();
    ota_init();

    board_init(settings()->rotate_180);
    float cal[6];
    if (settings_get_touch_cal(cal)) dk_gsl3680_set_mapping(cal, rotation());
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
