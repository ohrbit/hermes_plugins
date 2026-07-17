/*
 * audio_capture.c — I2S mic capture + energy-based VAD. DRAFT (no HW yet).
 *
 * v6 API NOTE: the legacy driver/i2s.h API (i2s_config_t, i2s_driver_install,
 * i2s_set_pin, i2s_read, I2S_NUM_0) was removed in ESP-IDF v6. The new driver
 * lives in esp_driver_i2s (i2s_new_channel / i2s_channel_init_std_* /
 * i2s_channel_enable / i2s_channel_read). This file is a STUB: it compiles
 * against v6 and returns sane defaults, but does NOT touch real hardware yet.
 * TODO(esp-idf-v6): implement the real I2S std-RX channel init here once a
 * Stick-S3 board is available to validate pin/io against.
 */

#include "audio_capture.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include <esp_err.h>

static const char *TAG = "eh_capture";
static bool s_init = false;
static bool s_running = false;
static int  s_vad_sens = 50;     /* 0..100 */

/* forward decl (defined at bottom of file) */
static uint32_t isqrt(uint32_t x);

/* M5Stack Stick-S3 mic pins (board defaults; captured for the future port). */
static const int I2S_MIC_WS  = 8;
static const int I2S_MIC_SCK = 9;
static const int I2S_MIC_SD  = 10;

esp_err_t eh_audio_capture_init(void) {
    if (s_init) return ESP_OK;
    /* TODO(esp-idf-v6): real I2S std-RX channel init via esp_driver_i2s.
     * Stubbed so the firmware links; no hardware access yet. */
    ESP_LOGW(TAG, "capture init STUBBED (v6 I2S not wired) ws=%d sck=%d sd=%d",
             I2S_MIC_WS, I2S_MIC_SCK, I2S_MIC_SD);
    s_init = true;
    return ESP_OK;
}

esp_err_t eh_audio_capture_start(void) {
    if (!s_init) return ESP_ERR_INVALID_STATE;
    s_running = true;
    return ESP_OK;
}

esp_err_t eh_audio_capture_stop(void) {
    if (!s_running) return ESP_OK;
    s_running = false;
    return ESP_OK;
}

int eh_audio_capture_read(int16_t *buf, int capacity) {
    if (!s_running || !buf || capacity <= 0) return -1;
    /* STUB: no real samples. Return 0 (silence) so callers don't block. */
    memset(buf, 0, (size_t)capacity * sizeof(int16_t));
    return 0;
}

bool eh_audio_vad_active(void) {
    if (!s_running) return false;
    /* STUB: VAD always reports silence. Real RMS gate deferred to HW port. */
    return false;
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
