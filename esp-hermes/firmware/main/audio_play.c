/*
 * audio_play.c — I2S speaker output for TTS downlink + local audio cues (§16).
 *
 * Ported to the ESP-IDF v6 I2S driver (esp_driver_i2s): the legacy
 * driver/i2s.h API was removed. v6 uses i2s_new_channel +
 * i2s_channel_init_std_mode + i2s_channel_enable + i2s_channel_write.
 * NOTE: this drives the raw I2S data line. On the Stick S3 the ES8311 codec
 * (if present) would additionally need esp_codec_dev init; that is a separate
 * TODO(esp-hermes) — the I2S transport itself is wired here.
 */

#include "audio_play.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"
#include <esp_err.h>
#include "driver/i2s_std.h"
#include "esp_hermes.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static const char *TAG = "eh_play";
static bool s_init = false;
static i2s_chan_handle_t s_tx_chan = NULL;

/* M5Stack Stick-S3 built-in speaker pins (board defaults, from esp_hermes.h). */
#define I2S_SPK_WS  EH_PIN_SPK_WS
#define I2S_SPK_SCK EH_PIN_SPK_SCK
#define I2S_SPK_SD  EH_PIN_SPK_SD

esp_err_t eh_audio_play_init(void) {
    if (s_init) return ESP_OK;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    esp_err_t e = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
    if (e != ESP_OK) return e;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = I2S_SPK_SCK,
            .ws   = I2S_SPK_WS,
            .dout = I2S_SPK_SD,
            .din  = GPIO_NUM_NC,
        },
    };
    e = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (e != ESP_OK) { i2s_del_channel(s_tx_chan); s_tx_chan = NULL; return e; }

    s_init = true;
    ESP_LOGI(TAG, "play init OK (spk ws=%d sck=%d sd=%d)", I2S_SPK_WS, I2S_SPK_SCK, I2S_SPK_SD);
    return ESP_OK;
}

int eh_audio_play_pcm(const int16_t *pcm, int samples) {
    if (!s_init || !pcm || samples <= 0) return -1;
    size_t written = 0;
    esp_err_t e = i2s_channel_write(s_tx_chan, pcm,
                                    (size_t)samples * sizeof(int16_t),
                                    &written, pdMS_TO_TICKS(1000));
    if (e != ESP_OK) return -1;
    return (int)(written / sizeof(int16_t));
}

esp_err_t eh_audio_play_tone(uint16_t freq, uint32_t ms, float vol) {
    if (!s_init) return ESP_ERR_INVALID_STATE;
    if (vol < 0) { vol = 0; }
    if (vol > 1) { vol = 1; }
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
}
