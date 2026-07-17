/*
 * audio_capture.c — I2S mic capture + energy-based VAD. DRAFT (no HW yet).
 *
 * Ported to the ESP-IDF v6 I2S driver (esp_driver_i2s): the legacy
 * driver/i2s.h API (i2s_config_t / i2s_driver_install / i2s_set_pin /
 * i2s_read / I2S_NUM_0) was removed. v6 uses i2s_new_channel +
 * i2s_channel_init_std_mode + i2s_channel_enable/disable + i2s_channel_read.
 *
 * Pins are taken from esp_hermes.h board map (Stick S3). MCLK is left unused
 * (GPIO_NUM_NC) where the board does not expose it for the mic.
 */

#include "audio_capture.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include <esp_err.h>
#include "driver/i2s_std.h"
#include "esp_hermes.h"

static const char *TAG = "eh_capture";
static bool s_init = false;
static bool s_running = false;
static int  s_vad_sens = 50;     /* 0..100 */

static i2s_chan_handle_t s_rx_chan = NULL;

/* forward decl */
static uint32_t isqrt(uint32_t x);

/* M5Stack Stick-S3 mic pins (board defaults, from esp_hermes.h). */
#define I2S_MIC_WS  EH_PIN_MIC_WS
#define I2S_MIC_SCK EH_PIN_MIC_SCK
#define I2S_MIC_SD  EH_PIN_MIC_SD

esp_err_t eh_audio_capture_init(void) {
    if (s_init) return ESP_OK;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t e = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (e != ESP_OK) return e;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = I2S_MIC_SCK,
            .ws   = I2S_MIC_WS,
            .dout = GPIO_NUM_NC,
            .din  = I2S_MIC_SD,
        },
    };
    e = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (e != ESP_OK) { i2s_del_channel(s_rx_chan); s_rx_chan = NULL; return e; }

    s_init = true;
    ESP_LOGI(TAG, "capture init OK (mic ws=%d sck=%d sd=%d)", I2S_MIC_WS, I2S_MIC_SCK, I2S_MIC_SD);
    return ESP_OK;
}

esp_err_t eh_audio_capture_start(void) {
    if (!s_init) return ESP_ERR_INVALID_STATE;
    if (s_running) return ESP_OK;
    esp_err_t e = i2s_channel_enable(s_rx_chan);
    if (e == ESP_OK) s_running = true;
    return e;
}

esp_err_t eh_audio_capture_stop(void) {
    if (!s_running) return ESP_OK;
    s_running = false;
    return i2s_channel_disable(s_rx_chan);
}

bool eh_audio_capture_running(void) {
    return s_running;
}

int eh_audio_capture_read(int16_t *buf, int capacity) {
    if (!s_running || !buf || capacity <= 0) return -1;
    size_t bytes_read = 0;
    esp_err_t e = i2s_channel_read(s_rx_chan, buf,
                                   (size_t)capacity * sizeof(int16_t),
                                   &bytes_read, pdMS_TO_TICKS(100));
    if (e != ESP_OK) return -1;
    return (int)(bytes_read / sizeof(int16_t));
}

bool eh_audio_vad_active(void) {
    if (!s_running) return false;
    int16_t buf[256];
    int n = eh_audio_capture_read(buf, 256);
    if (n <= 0) return false;
    /* RMS energy gate. Threshold scales inverse with sensitivity. */
    int64_t sum = 0;
    for (int i = 0; i < n; i++) sum += (int32_t)buf[i] * buf[i];
    int rms = (int)isqrt((uint32_t)(sum / n));
    int threshold = 400 + (100 - s_vad_sens) * 20;  /* ~400..2400 */
    return rms > threshold;
}

void eh_audio_vad_set_sensitivity(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_vad_sens = pct;
}

/* tiny integer sqrt (no libc dependency for the draft). */
static uint32_t isqrt(uint32_t x) {
    uint32_t r = 0, b = 0x40000000u;
    while (b > x) b >>= 2;
    while (b != 0) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else            { r >>= 1; }
        b >>= 2;
    }
    return r;
}
