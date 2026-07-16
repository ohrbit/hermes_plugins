/*
 * audio_play.c — I2S speaker + tone generator for local cues. DRAFT (no HW).
 *
 * TTS audio arrives from the gateway already decoded to PCM (we decode the
 * WS 'audio' message, then play). Local cues (§16) are synthesized on-device so
 * feedback is instant and works offline.
 */
#include "audio_play.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "driver/i2s.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static const char *TAG = "eh_play";
static bool s_init = false;
/* M5Stack Stick-S3 built-in speaker on I2S1. Pins are board defaults. */
static const int I2S_SPK_WS  = 15;
static const int I2S_SPK_SCK = 16;
static const int I2S_SPK_SD  = 17;
#define I2S_SPK_PORT I2S_NUM_1

esp_err_t eh_audio_play_init(void) {
    if (s_init) return ESP_OK;
    i2s_config_t cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = true,    /* cleaner tones */
    };
    i2s_pin_config_t pins = {
        .bck_io_num = I2S_SPK_SCK,
        .ws_io_num = I2S_SPK_WS,
        .data_out_num = I2S_SPK_SD,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };
    esp_err_t e = i2s_driver_install(I2S_SPK_PORT, &cfg, 0, NULL);
    if (e != ESP_OK) return e;
    e = i2s_set_pin(I2S_SPK_PORT, &pins);
    if (e != ESP_OK) { i2s_driver_uninstall(I2S_SPK_PORT); return e; }
    s_init = true;
    return i2s_start(I2S_SPK_PORT);
}

int eh_audio_play_pcm(const int16_t *pcm, int samples) {
    if (!s_init || !pcm || samples <= 0) return -1;
    size_t written = 0;
    i2s_write(I2S_SPK_PORT, pcm, samples * sizeof(int16_t), &written,
              pdMS_TO_TICKS(1000));
    return (int)(written / sizeof(int16_t));
}

esp_err_t eh_audio_play_tone(uint16_t freq, uint32_t ms, float vol) {
    if (!s_init) return ESP_ERR_INVALID_STATE;
    if (vol < 0) vol = 0; if (vol > 1) vol = 1;
    const int SR = 16000;
    int n = (int)((uint64_t)SR * ms / 1000);
    int16_t *buf = malloc(n * sizeof(int16_t));
    if (!buf) return ESP_ERR_NO_MEM;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        buf[i] = (int16_t)(vol * 32767 * sinf(2 * M_PI * freq * t));
    }
    eh_audio_play_pcm(buf, n);
    free(buf);
    return ESP_OK;
}

void eh_audio_cue(eh_pet_state_t state) {
    switch (state) {
        case EH_PET_RUN:    eh_audio_play_tone(660, 60, 0.3); break;  /* busy tick */
        case EH_PET_DONE:   eh_audio_play_tone(880, 90, 0.4);
                           eh_audio_play_tone(1175, 110, 0.4); break; /* 2-note */
        case EH_PET_ERROR:  eh_audio_play_tone(160, 200, 0.4); break; /* low buzz */
        default: break;     /* idle: no cue */
    }
    /* mode switch click handled separately by caller via eh_audio_play_tone */
}
