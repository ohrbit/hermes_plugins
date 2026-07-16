/*
 * audio_capture.h — I2S microphone capture + (optional) on-device VAD.
 * PTT mode: caller starts/stops recording around a button hold. VAD mode:
 * run eh_audio_vad_active() continuously; it returns true only on speech so we
 * don't stream silence (spec §7, pitfalls: no VAD => constant stream).
 */
#ifndef EH_AUDIO_CAPTURE_H
#define EH_AUDIO_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_hermes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize I2S mic (board-specific pins resolved at runtime). */
esp_err_t eh_audio_capture_init(void);

/* Begin/end a capture session (PTT press/release). */
esp_err_t eh_audio_capture_start(void);
esp_err_t eh_audio_capture_stop(void);

/*
 * Pull up to `capacity` int16 samples (mono). Returns number of samples read
 * (0 if none available) or <0 on error. Caller encodes + ships upstream.
 */
int eh_audio_capture_read(int16_t *buf, int capacity);

/* Lightweight on-device VAD (spec §7). Returns true if recent energy/RMS
 * crosses the speech threshold. Used only in EH_MODE_VAD. */
bool eh_audio_vad_active(void);

/* Set VAD sensitivity 0..100 (higher = more sensitive). */
void eh_audio_vad_set_sensitivity(int pct);

#ifdef __cplusplus
}
#endif
#endif /* EH_AUDIO_CAPTURE_H */
