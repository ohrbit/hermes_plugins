/*
 * audio_codec.c — DRAFT codec. Falls back to PCM pass-through; Opus is a stub
 * marked TODO so the build compiles and the pipeline is testable as PCM.
 *
 * To enable Opus on hardware: add libopus (idf_component.yml), then replace the
 * EH_AUDIO_OPUS branches with real encoder/decoder calls. The function
 * signatures already match a typical Opus API so the swap is local.
 */
#include "audio_codec.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "eh_audio";
static eh_audio_format_t s_fmt = EH_AUDIO_PCM;

eh_audio_format_t eh_audio_negotiated_format(void) { return s_fmt; }
void eh_audio_set_format(eh_audio_format_t fmt) { s_fmt = fmt; }

int eh_audio_encode(eh_audio_format_t fmt,
                    const int16_t *pcm, size_t pcm_samples,
                    uint8_t *out, size_t out_cap) {
    if (!pcm || !out) return -1;
    switch (fmt) {
        case EH_AUDIO_PCM:
        case EH_AUDIO_BINARY: {
            size_t bytes = pcm_samples * sizeof(int16_t);
            if (bytes > out_cap) return -1;
            memcpy(out, pcm, bytes);
            return (int)bytes;
        }
        case EH_AUDIO_OPUS:
            /* TODO(hw): encode via libopus. PCM pass-through placeholder keeps
             * the pipeline runnable for early bring-up over a fast local link. */
            ESP_LOGW(TAG, "Opus encode not yet wired; sending PCM");
            {
                size_t bytes = pcm_samples * sizeof(int16_t);
                if (bytes > out_cap) return -1;
                memcpy(out, pcm, bytes);
                return (int)bytes;
            }
        default:
            return -1;
    }
}

int eh_audio_decode(eh_audio_format_t fmt,
                    const uint8_t *in, size_t in_len,
                    int16_t *pcm, size_t pcm_cap) {
    if (!in || !pcm) return -1;
    switch (fmt) {
        case EH_AUDIO_PCM:
        case EH_AUDIO_BINARY: {
            size_t samples = in_len / sizeof(int16_t);
            if (samples > pcm_cap) samples = pcm_cap;
            memcpy(pcm, in, samples * sizeof(int16_t));
            return (int)samples;
        }
        case EH_AUDIO_OPUS:
            ESP_LOGW(TAG, "Opus decode not yet wired; expecting PCM");
            {
                size_t samples = in_len / sizeof(int16_t);
                if (samples > pcm_cap) samples = pcm_cap;
                memcpy(pcm, in, samples * sizeof(int16_t));
                return (int)samples;
            }
        default:
            return -1;
    }
}
