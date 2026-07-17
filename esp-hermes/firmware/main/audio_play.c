/*
 * audio_play.c — I2S speaker + tone generator for local cues. DRAFT (no HW).
 *
 * v6 API NOTE: driver/i2s.h (i2s_config_t, i2s_driver_install, i2s_set_pin,
 * i2s_write, I2S_NUM_1) was removed in ESP-IDF v6. New API is esp_driver_i2s
 * (i2s_new_channel / i2s_channel_init_std_tx / i2s_channel_enable /
 * i2s_channel_write). This file is a STUB: it compiles against v6 and returns
 * sane defaults without touching hardware.
 * TODO(esp-idf-v6): implement real I2S std-TX channel init (ES8311 DAC on the
 * Stick-S3) once a board is available to validate against.
 */

#include "audio_play.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"
#include <esp_err.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static const char *TAG = "eh_play";
static bool s_init = false;
/* M5Stack Stick-S3 built-in speaker pins (board defaults; for the future port). */
static const int I2S_SPK_WS  = 15;
static const int I2S_SPK_SCK = 16;
static const int I2S_SPK_SD  = 17;

esp_err_t eh_audio_play_init(void) {
    if (s_init) return ESP_OK;
    /* TODO(esp-idf-v6): real I2S std-TX channel init via esp_driver_i2s. */
    ESP_LOGW(TAG, "play init STUBBED (v6 I2S not wired) ws=%d sck=%d sd=%d",
             I2S_SPK_WS, I2S_SPK_SCK, I2S_SPK_SD);
    s_init = true;
    return ESP_OK;
}

int eh_audio_play_pcm(const int16_t *pcm, int samples) {
    if (!s_init || !pcm || samples <= 0) return -1;
    /* STUB: discard samples. Return the requested count as "played". */
    return samples;
}

esp_err_t eh_audio_play_tone(uint16_t freq, uint32_t ms, float vol) {
    if (!s_init) return ESP_ERR_INVALID_STATE;
    if (vol < 0) { vol = 0; }
    if (vol > 1) { vol = 1; }
    /* STUB: no real tone generated. Kept for API compatibility. */
    (void)freq; (void)ms; (void)vol;
    return ESP_OK;
}

void eh_audio_cue(eh_pet_state_t state) {
    /* STUB: cues are silent until the audio codec is wired. */
    (void)state;
}
