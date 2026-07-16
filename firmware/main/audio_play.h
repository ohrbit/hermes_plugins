/*
 * audio_play.h — I2S speaker output for TTS downlink + local audio cues (§16).
 */
#ifndef EH_AUDIO_PLAY_H
#define EH_AUDIO_PLAY_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_hermes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize I2S speaker (MonoPPA / MAX98357 / built-in per board). */
esp_err_t eh_audio_play_init(void);

/* Play already-decoded PCM samples (from gateway TTS downlink). Blocking
 * until the buffer is queued; returns samples accepted. */
int eh_audio_play_pcm(const int16_t *pcm, int samples);

/* Local synthesized cue (§16): tone, no TTS. freq Hz, ms duration, vol 0..1. */
esp_err_t eh_audio_play_tone(uint16_t freq, uint32_t ms, float vol);

/* Convenience cues mapped to pet states (§16). */
void eh_audio_cue(eh_pet_state_t state);

#ifdef __cplusplus
}
#endif
#endif /* EH_AUDIO_PLAY_H */
