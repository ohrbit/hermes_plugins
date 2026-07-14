/*
 * audio_capture.c — I2S mic capture + energy-based VAD. DRAFT (no HW yet).
 *
 * The VAD here is a simple RMS/energy gate good enough to suppress silence in
 * always-on mode. A proper WebRTC-VAD port is a Phase-2 improvement; energy
 * gating keeps the firmware dependency-free for the draft and avoids streaming
 * silence (a documented pitfall in the skill).
 */
#include "audio_capture.h"
#include <string.h>
#include "esp_log.h"
#include "driver/i2s.h"

static const char *TAG = "eh_capture";
static bool s_init = false;
static bool s_running = false;
static int  s_vad_sens = 50;     /* 0..100 */

/* forward decl (defined at bottom of file) */
static uint32_t isqrt(uint32_t x);
/* M5Stack Stick-S3 mic is on the internal I2S; pins are board defaults. */
static const int I2S_MIC_WS  = 8;
static const int I2S_MIC_SCK = 9;
static const int I2S_MIC_SD  = 10;
#define I2S_MIC_PORT  I2S_NUM_0

esp_err_t eh_audio_capture_init(void) {
    if (s_init) return ESP_OK;
    i2s_config_t cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false,
    };
    i2s_pin_config_t pins = {
        .bck_io_num = I2S_MIC_SCK,
        .ws_io_num = I2S_MIC_WS,
        .data_in_num = I2S_MIC_SD,
        .data_out_num = I2S_PIN_NO_CHANGE,
    };
    esp_err_t e = i2s_driver_install(I2S_MIC_PORT, &cfg, 0, NULL);
    if (e != ESP_OK) return e;
    e = i2s_set_pin(I2S_MIC_PORT, &pins);
    if (e != ESP_OK) { i2s_driver_uninstall(I2S_MIC_PORT); return e; }
    s_init = true;
    return ESP_OK;
}

esp_err_t eh_audio_capture_start(void) {
    if (!s_init) return ESP_ERR_INVALID_STATE;
    if (s_running) return ESP_OK;
    s_running = true;
    return i2s_start(I2S_MIC_PORT);
}
esp_err_t eh_audio_capture_stop(void) {
    if (!s_running) return ESP_OK;
    s_running = false;
    return i2s_stop(I2S_MIC_PORT);
}

int eh_audio_capture_read(int16_t *buf, int capacity) {
    if (!s_running || !buf || capacity <= 0) return -1;
    size_t bytes_read = 0;
    esp_err_t e = i2s_read(I2S_MIC_PORT, buf,
                           capacity * sizeof(int16_t), &bytes_read,
                           pdMS_TO_TICKS(100));
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
