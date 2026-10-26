#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "gsl3680.h"
#include "jd9365_init.h"
#include "settings.h"
#include "sdkconfig.h"

static const char *TAG = "board";

// Kconfig leaves disabled booleans undefined, so turn them into 0/1.
#ifdef CONFIG_DESKLING_LCD_V2
#define LCD_IS_V2 1
#else
#define LCD_IS_V2 0
#endif
#ifdef CONFIG_DESKLING_TOUCH_SWAP_XY
#define TOUCH_SWAP_XY 1
#else
#define TOUCH_SWAP_XY 0
#endif
#ifdef CONFIG_DESKLING_TOUCH_MIRROR_X
#define TOUCH_MIRROR_X 1
#else
#define TOUCH_MIRROR_X 0
#endif
#ifdef CONFIG_DESKLING_TOUCH_MIRROR_Y
#define TOUCH_MIRROR_Y 1
#else
#define TOUCH_MIRROR_Y 0
#endif
#ifdef CONFIG_DESKLING_ROTATE_180
#define ROTATE_180 1
#else
#define ROTATE_180 0
#endif

#define BL_TIMER LEDC_TIMER_1
#define BL_CHANNEL LEDC_CHANNEL_1

static void backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BL_TIMER,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    const ledc_channel_config_t ch = {
        .gpio_num = BOARD_LCD_BL_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_CHANNEL,
        .timer_sel = BL_TIMER,
        .duty = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

void board_set_backlight(uint8_t percent)
{
    if (percent > 100) percent = 100;
    // Scale the UI's 0..100 (100 = day, 25 = night) by the saved daytime brightness.
    uint32_t duty = (uint32_t)percent * settings()->brightness * 1023 / 10000;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}

static esp_err_t panel_init(esp_lcd_panel_io_handle_t *out_io, esp_lcd_panel_handle_t *out_panel)
{
    // The MIPI D-PHY is powered from the on-chip LDO.
    static esp_ldo_channel_handle_t ldo;
    const esp_ldo_channel_config_t ldo_cfg = {.chan_id = BOARD_MIPI_PHY_LDO_CHAN, .voltage_mv = BOARD_MIPI_PHY_LDO_MV};
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &ldo), TAG, "MIPI PHY LDO");

    esp_lcd_dsi_bus_handle_t bus;
    const esp_lcd_dsi_bus_config_t bus_cfg = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &bus), TAG, "DSI bus");

    const esp_lcd_dbi_io_config_t dbi_cfg = JD9365_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, out_io), TAG, "DBI IO");

    esp_lcd_dpi_panel_config_t dpi = JD9365_800_1280_PANEL_60HZ_DPI_CONFIG_CF(LCD_COLOR_FMT_RGB565);
#if CONFIG_DESKLING_LCD_V2
    dpi.dpi_clock_freq_mhz = 70;
    dpi.video_timing.vsync_back_porch = 4;
    dpi.video_timing.vsync_pulse_width = 10;
    dpi.video_timing.vsync_front_porch = 30;
#else
    dpi.dpi_clock_freq_mhz = 60;
    dpi.video_timing.vsync_back_porch = 8;
    dpi.video_timing.vsync_pulse_width = 4;
    dpi.video_timing.vsync_front_porch = 20;
#endif
    dpi.num_fbs = 1;

    jd9365_vendor_config_t vendor = {
        .init_cmds = s_lcd_init_cmds,
        .init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]),
        .mipi_config = {.dsi_bus = bus, .dpi_config = &dpi, .lane_num = 2},
    };
    const esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = BOARD_LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9365(*out_io, &dev, out_panel), TAG, "JD9365 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*out_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*out_panel), TAG, "panel init");
    esp_lcd_panel_disp_on_off(*out_panel, true);
    ESP_LOGI(TAG, "panel up (LCD %s)", LCD_IS_V2 ? "V2" : "V1");
    return ESP_OK;
}


// ------------------------------------------------------------ frame timing
// How long each redraw takes on the real hardware, logged every 5 s while things move and
// shown on the setup page, so slow screens can be measured instead of guessed at.
static struct {
    int64_t t0;
    bool rendered;
    uint32_t frames;
    uint64_t sum_us;
    uint32_t worst_us;
    board_display_stats_t last;
} s_perf;

static void perf_event(lv_event_t *e)
{
    switch (lv_event_get_code(e)) {
    case LV_EVENT_REFR_START:
        s_perf.t0 = esp_timer_get_time();
        s_perf.rendered = false;
        break;
    case LV_EVENT_RENDER_START:
        s_perf.rendered = true;
        break;
    case LV_EVENT_REFR_READY:
        if (s_perf.rendered) {
            uint32_t us = (uint32_t)(esp_timer_get_time() - s_perf.t0);
            s_perf.frames++;
            s_perf.sum_us += us;
            if (us > s_perf.worst_us) s_perf.worst_us = us;
        }
        break;
    default:
        break;
    }
}

static void perf_report(lv_timer_t *t)
{
    (void)t;
    if (!s_perf.frames) return;
    s_perf.last.frames = s_perf.frames;
    s_perf.last.avg_ms = (float)s_perf.sum_us / s_perf.frames / 1000.0f;
    s_perf.last.worst_ms = s_perf.worst_us / 1000.0f;
    s_perf.last.busy_pct = (uint8_t)(s_perf.sum_us / 50000); // of 5 s
    ESP_LOGI(TAG, "display: %u redraws in 5 s, %.1f ms each on average, worst %.1f ms, busy %u%%", (unsigned)s_perf.frames,
             s_perf.last.avg_ms, s_perf.last.worst_ms, s_perf.last.busy_pct);
    s_perf.frames = 0;
    s_perf.sum_us = 0;
    s_perf.worst_us = 0;
}

board_display_stats_t board_display_stats(void)
{
    return s_perf.last;
}

static void perf_init(lv_display_t *disp)
{
    lv_display_add_event_cb(disp, perf_event, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(disp, perf_event, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, perf_event, LV_EVENT_REFR_READY, NULL);
    lv_timer_create(perf_report, 5000, NULL);
}

static i2c_master_bus_handle_t s_i2c; // shared by touch and the audio codec

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_i2c;
}

static esp_lcd_touch_handle_t touch_init(void)
{
    i2c_master_bus_handle_t i2c;
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = BOARD_TOUCH_SDA_GPIO,
        .scl_io_num = BOARD_TOUCH_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &i2c) != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus failed: no touch");
        return NULL;
    }
    s_i2c = i2c;
    esp_lcd_panel_io_handle_t io;
    const esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = GSL3680_I2C_ADDRESS,
        .scl_speed_hz = 400000, // must be set: 0 makes every transaction fail on IDF 5.4+
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags.disable_control_phase = 1,
        .transaction_timeout_ms = 100,
    };
    if (esp_lcd_new_panel_io_i2c(i2c, &io_cfg, &io) != ESP_OK) return NULL;

    const esp_lcd_touch_config_t cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = BOARD_TOUCH_RST_GPIO,
        // Polled, not interrupt-driven: with an INT pin, esp_lvgl_port only reads the panel after
        // an interrupt, and the GSL3680's INT edge isn't reliable across board batches. Polling
        // costs one small I2C read per LVGL input tick (~30 ms).
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {.reset = 0, .interrupt = 0},
        .flags =
            {
                .swap_xy = TOUCH_SWAP_XY,
                .mirror_x = TOUCH_MIRROR_X,
                .mirror_y = TOUCH_MIRROR_Y,
            },
    };
    esp_lcd_touch_handle_t tp = NULL;
    if (dk_gsl3680_new(io, &cfg, &tp) != ESP_OK) return NULL;
    dk_gsl3680_set_mapping(NULL, ROTATE_180 ? 270 : 90);
    return tp;
}

lv_display_t *board_init(void)
{
    backlight_init();
    board_set_backlight(0);

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    ESP_ERROR_CHECK(panel_init(&io, &panel));

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 4;
    port_cfg.task_stack = 16 * 1024;
    port_cfg.task_affinity = 1;
    port_cfg.timer_period_ms = 5;
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    // LVGL renders the screen in horizontal bands, the PPA rotates each band into the panel's
    // portrait framebuffer, and DMA2D copies it in while LVGL renders the next band. Bands in
    // internal RAM render much faster than in PSRAM (which the panel is also streaming the
    // framebuffer out of 60 times a second), so use internal RAM when there's room for the two
    // bands plus the PPA's output band and still plenty left for Wi-Fi and the rest.
    size_t band_px = BOARD_LCD_H_RES * 160; // PSRAM fallback: 1280 x 100 lines
    bool internal = false;
    const size_t margin = 160 * 1024;
    for (size_t lines = 40; lines >= 20 && !internal; lines /= 2) {
        size_t px = BOARD_LCD_H_RES * (lines * BOARD_LCD_V_RES / BOARD_LCD_H_RES); // lines of the 1280-wide landscape
        size_t bytes = px * 2;
        size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (largest >= bytes + 1024 && free_dma >= 3 * bytes + margin) {
            band_px = px;
            internal = true;
        }
    }
    ESP_LOGI(TAG, "render bands: %u px each in %s (internal free %u KB)", (unsigned)band_px, internal ? "internal RAM" : "PSRAM",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = band_px,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags =
            {
                .buff_dma = internal,
                .buff_spiram = !internal,
                .sw_rotate = true, // done by the P4's PPA engine
            },
    };
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {.flags = {.avoid_tearing = false}};
    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    assert(disp);
    // Copy rotated bands into the framebuffer with DMA2D instead of a CPU memcpy, so the CPU
    // can get on with the next band.
    if (esp_lcd_dpi_panel_enable_dma2d(panel) != ESP_OK) ESP_LOGW(TAG, "DMA2D unavailable, copying bands with the CPU");

    lvgl_port_lock(0);
    lv_display_set_rotation(disp, ROTATE_180 ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90);
    perf_init(disp);
    lvgl_port_unlock();

    esp_lcd_touch_handle_t tp = touch_init();
    if (tp) {
        const lvgl_port_touch_cfg_t touch_cfg = {.disp = disp, .handle = tp};
        lvgl_port_add_touch(&touch_cfg);
    }
    return disp;
}
