#include "audio.h"

#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "audio";

// Pins from Guition's board package for the JC8012P4A1C.
#define AUDIO_I2S_MCLK 13
#define AUDIO_I2S_BCLK 12
#define AUDIO_I2S_WS 10
#define AUDIO_I2S_DOUT 9
#define AUDIO_I2S_DIN 11
#define AUDIO_PA_GPIO 20

#define CHUNK 240          // 10 ms at 24 kHz
#define IDLE_CLOSE_MS 4000 // power the amplifier down after this much quiet

static QueueHandle_t s_queue;
static esp_codec_dev_handle_t s_dev;
static volatile uint8_t s_volume = 75;
static uint8_t s_applied = 255;
static bool s_open;

static bool codec_open(void)
{
    if (s_open) return true;
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = DK_SOUND_RATE,
        .channel = 1,
        .bits_per_sample = 16,
    };
    if (esp_codec_dev_open(s_dev, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "couldn't open the codec");
        return false;
    }
    s_applied = 255; // re-apply after every open
    s_open = true;
    return true;
}

static void codec_close(void)
{
    if (!s_open) return;
    esp_codec_dev_close(s_dev);
    s_open = false;
}

static void audio_task(void *arg)
{
    (void)arg;
    static int16_t buf[CHUNK];
    dk_sound_t cur = DK_SND_NONE;
    int pos = 0, len = 0;
    for (;;) {
        dk_sound_t next;
        // While playing, just peek for interruptions; when idle, wait (and power down after a while).
        TickType_t wait = cur != DK_SND_NONE ? 0 : (s_open ? pdMS_TO_TICKS(IDLE_CLOSE_MS) : portMAX_DELAY);
        if (xQueueReceive(s_queue, &next, wait) == pdTRUE) {
            if (s_volume == 0) continue;
            cur = next;
            pos = 0;
            len = sound_length(cur) + CHUNK * 2; // a little silence at the end flushes the DMA
            if (!codec_open()) cur = DK_SND_NONE;
        } else if (cur == DK_SND_NONE) {
            codec_close();
            continue;
        }
        if (cur == DK_SND_NONE) continue;
        if (s_applied != s_volume) {
            s_applied = s_volume;
            esp_codec_dev_set_out_vol(s_dev, s_applied);
        }
        sound_render(cur, pos, CHUNK, buf);
        esp_codec_dev_write(s_dev, buf, sizeof(buf));
        pos += CHUNK;
        if (pos >= len) cur = DK_SND_NONE;
    }
}

void audio_init(i2c_master_bus_handle_t i2c)
{
    s_queue = xQueueCreate(4, sizeof(dk_sound_t));
    if (!i2c) {
        ESP_LOGW(TAG, "no I2C bus, so no sound");
        return;
    }

    i2s_chan_handle_t tx = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // send silence when we run out of data, instead of repeating the last buffer
    if (i2s_new_channel(&chan_cfg, &tx, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "no free I2S channel");
        return;
    }
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(DK_SOUND_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
            {
                .mclk = AUDIO_I2S_MCLK,
                .bclk = AUDIO_I2S_BCLK,
                .ws = AUDIO_I2S_WS,
                .dout = AUDIO_I2S_DOUT,
                .din = I2S_GPIO_UNUSED,
            },
    };
    if (i2s_channel_init_std_mode(tx, &std) != ESP_OK || i2s_channel_enable(tx) != ESP_OK) {
        ESP_LOGW(TAG, "I2S setup failed");
        return;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {.port = I2S_NUM_0, .tx_handle = tx};
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    audio_codec_i2c_cfg_t i2c_cfg = {.port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = i2c};
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (!data_if || !ctrl_if || !gpio_if) {
        ESP_LOGW(TAG, "codec interfaces failed");
        return;
    }
    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = AUDIO_PA_GPIO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3},
    };
    const audio_codec_if_t *codec = es8311_codec_new(&es_cfg);
    if (!codec) {
        ESP_LOGW(TAG, "ES8311 didn't answer on I2C");
        return;
    }
    esp_codec_dev_cfg_t dev_cfg = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec, .data_if = data_if};
    s_dev = esp_codec_dev_new(&dev_cfg);
    if (!s_dev) return;
    // The default curve is linear in dB from -50 to 0, so 50% is a whisper on this little speaker.
    // This one is shaped for ears: 25% is quiet, 50% is clearly audible, 100% is as loud as it goes.
    static esp_codec_dev_vol_map_t points[] = {{0, -60.0f}, {1, -38.0f}, {25, -20.0f}, {50, -10.0f}, {75, -4.0f}, {100, 0.0f}};
    esp_codec_dev_vol_curve_t curve = {.vol_map = points, .count = sizeof(points) / sizeof(points[0])};
    esp_codec_dev_set_vol_curve(s_dev, &curve);
    xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 6, NULL, 0);
    ESP_LOGI(TAG, "speaker ready");
}

void audio_play(dk_sound_t s)
{
    if (!s_queue || !s_dev || s == DK_SND_NONE || s_volume == 0) return;
    if (xQueueSend(s_queue, &s, 0) != pdTRUE) {
        // Full: the newest sound matters most.
        dk_sound_t drop;
        xQueueReceive(s_queue, &drop, 0);
        xQueueSend(s_queue, &s, 0);
    }
}

void audio_set_volume(uint8_t percent)
{
    s_volume = percent > 100 ? 100 : percent; // the playback task applies it
}
