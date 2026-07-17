/*
 * lcd_viz.h — real-time audio visualizer API for the Stick S3 LCD.
 */
#ifndef EH_LCD_VIZ_H
#define EH_LCD_VIZ_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the visualizer offscreen buffer. Call once after LCD init. */
void eh_lcd_viz_init(void);

/* Feed one capture frame (int16 PCM, `n` samples) to update + redraw. */
void eh_lcd_viz_update(const int16_t *samples, int n);

#ifdef __cplusplus
}
#endif
#endif /* EH_LCD_VIZ_H */
