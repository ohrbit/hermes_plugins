/*
 * lcd_pet.h — petdex sprite renderer on the ESP LCD (spec §6).
 *
 * The petdex renderer itself runs on the GATEWAY (Hermes side, petdex skill).
 * The gateway pushes either:
 *   - a "pet_state" message (we render the cached frame for that state), or
 *   - raw frame data / a "video" burst (gif/mjpeg) for event transitions.
 *
 * This module caches one RGB565 frame per agent state (downloaded once at
 * connect / on pet change) and blits it. It also supports drawing an optional
 * GIF/MJPEG burst passed as raw decoded RGB565 (decoder TBD with hardware).
 *
 * Physical IMU coupling (§6 Path A): the gateway maps motion to existing
 * states; here we additionally apply a cheap pose offset (lean) so the pet
 * "feels" alive when the device tilts. Path B custom states (tilt/shake/
 * stretch) are accepted as pet_state values and render the matching cached
 * frame if present, else fall back to idle.
 */
#ifndef EH_LCD_PET_H
#define EH_LCD_PET_H

#include <stdint.h>
#include "esp_hermes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EH_LCD_W 240
#define EH_LCD_H 240
#define EH_PET_BPP 2            /* RGB565 */
#define EH_PET_FRAME_BYTES (EH_LCD_W * EH_LCD_H * EH_PET_BPP)

/* Initialize the LCD driver (board-specific). */
esp_err_t eh_lcd_pet_init(void);

/* Set the active pet slug (re-fetch frames from gateway on next connect). */
void eh_lcd_pet_set_slug(const char *slug);

/* Push a decoded RGB565 frame for a given state (gateway-sent). Copies data. */
esp_err_t eh_lcd_pet_set_frame(eh_pet_state_t state,
                               const uint8_t *rgb565, size_t len);

/* Render the pet for `state`. pose_x/pose_y in [-100,100] are IMU lean
 * offsets in pixels (smoothly interpolated by caller). */
esp_err_t eh_lcd_pet_render(eh_pet_state_t state, int pose_x, int pose_y);

/* Blit an RGB565 bitmap into a sub-rectangle (used by the visualizer). */
esp_err_t eh_lcd_blit_rect(int x, int y, int w, int h, const uint8_t *rgb565);

/* Optional decoded video burst (RGB565 frames). TODO(hw): real GIF/MJPEG
 * decoder; for the draft we accept a single pre-decoded RGB565 buffer. */
esp_err_t eh_lcd_pet_play_burst(const uint8_t *rgb565, size_t len, uint32_t ms);

#ifdef __cplusplus
}
#endif
#endif /* EH_LCD_PET_H */
