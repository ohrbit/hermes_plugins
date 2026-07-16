/*
 * audio_codec.h — codec abstraction for the uplink/downlink audio path.
 *
 * DRAFT. Spec §11 leaves the codec decision (Opus vs PCM) open until hardware
 * arrives and bandwidth is measured. We abstract it so the rest of the firmware
 * does not care which one is active.
 *
 * Policy chosen for v1: PCM is the safe default (no decoder cost), Opus is the
 * bandwidth-saving upgrade. The ESP negotiates the chosen format in the audio
 * message "format" field and (for binary frames) via the negotiated capability.
 *
 * When Opus is wired (add libopus component), implement eh_audio_encode/decode
 * by calling the encoder. Until then they fall back to a straight PCM copy so
 * the pipeline is end-to-end testable with the gateway using raw PCM.
 */
#ifndef EH_AUDIO_CODEC_H
#define EH_AUDIO_CODEC_H

#include <stddef.h>
#include <stdint.h>
#include "esp_hermes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Encode one PCM frame buffer -> output buffer. Returns bytes written, or <0. */
int eh_audio_encode(eh_audio_format_t fmt,
                    const int16_t *pcm, size_t pcm_samples,
                    uint8_t *out, size_t out_cap);

/* Decode one compressed/raw buffer -> PCM. Returns samples written, or <0. */
int eh_audio_decode(eh_audio_format_t fmt,
                    const uint8_t *in, size_t in_len,
                    int16_t *pcm, size_t pcm_cap);

/* Human-readable negotiated format (for logs / capabilities). */
eh_audio_format_t eh_audio_negotiated_format(void);
void eh_audio_set_format(eh_audio_format_t fmt);

#ifdef __cplusplus
}
#endif
#endif /* EH_AUDIO_CODEC_H */
