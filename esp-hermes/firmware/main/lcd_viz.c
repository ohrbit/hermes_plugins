/*
 * lcd_viz.c — real-time audio visualizer for the Stick S3 LCD (240x240).
 *
 * Fed by the mic capture loop with int16 PCM samples. Computes a lightweight
 * energy spectrum (binned magnitude via simple envelope per band, no full FFT
 * needed on the S3) and draws:
 *   - a top "level meter" bar (overall RMS)
 *   - a row of frequency-ish bars across the width
 *   - an overlaid scrolling waveform
 *
 * The visualizer owns a small RGB565 offscreen strip it blits via
 * eh_lcd_blit_rect() so it never disturbs the cached pet frames.
 */

#include "lcd_viz.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_hermes.h"
#include "lcd_pet.h"

#define VIZ_X      0
#define VIZ_Y      0
#define VIZ_W      EH_LCD_W
#define VIZ_H      60              /* top band reserved for the viz */
#define NBANDS     24
#define WAVE_ROWS  2

static const char *TAG = "eh_viz";

/* RGB565 helpers */
#define RGB565(r,g,b) ((uint16_t)(((r&0x1F)<<11)|((g&0x3F)<<5)|(b&0x1F)))
#define BLACK    RGB565(0,0,0)
#define GREEN    RGB565(0,28,0)
#define CYAN     RGB565(0,28,28)
#define YELLOW   RGB565(28,28,0)
#define MAGENTA  RGB565(28,0,28)

/* offscreen strip for the viz band (persisted between frames) */
static uint8_t *s_viz = NULL;
static int16_t  s_wave[VIZ_W];     /* last row of waveform samples */
static int      s_whead = 0;

/* simple per-band envelope state */
static float s_band[NBANDS];

static void clear_viz(void) {
    for (int i = 0; i < VIZ_W * VIZ_H; i++) {
        ((uint16_t *)s_viz)[i] = BLACK;
    }
}

void eh_lcd_viz_init(void) {
    if (s_viz) return;
    s_viz = heap_caps_malloc(VIZ_W * VIZ_H * 2, MALLOC_CAP_DEFAULT);
    if (!s_viz) { ESP_LOGE(TAG, "viz alloc failed"); return; }
    memset(s_wave, 0, sizeof(s_wave));
    for (int i = 0; i < NBANDS; i++) s_band[i] = 0;
    clear_viz();
    ESP_LOGI(TAG, "viz init OK");
}

/* map sample index -> band (log-ish spacing) */
static int sample_to_band(int i, int n) {
    float t = (float)i / n;
    /* emphasize lows: band = floor(t^1.6 * NBANDS) */
    int b = (int)(powf(t, 1.6f) * NBANDS);
    return b >= NBANDS ? NBANDS - 1 : b;
}

void eh_lcd_viz_update(const int16_t *samples, int n) {
    if (!s_viz || n <= 0) return;

    /* 1) compute per-band envelope from this frame */
    float band_max[NBANDS];
    for (int i = 0; i < NBANDS; i++) band_max[i] = 0;
    for (int i = 0; i < n; i++) {
        int16_t s = samples[i];
        int b = sample_to_band(i, n);
        float mag = fabsf((float)s / 32768.0f);
        if (mag > band_max[b]) band_max[b] = mag;
    }
    /* decay + attack smoothing */
    for (int i = 0; i < NBANDS; i++) {
        float tgt = band_max[i];
        if (tgt > s_band[i]) s_band[i] = tgt;          /* fast attack */
        else s_band[i] = s_band[i] * 0.8f + tgt * 0.2f; /* slow decay */
    }

    /* 2) redraw */
    clear_viz();

    /* overall level meter (top 4px) */
    float rms = 0;
    for (int i = 0; i < n; i++) rms += (float)samples[i] * samples[i];
    rms = sqrtf(rms / n) / 32768.0f;
    int lvl = (int)(rms * VIZ_W);
    if (lvl > VIZ_W) lvl = VIZ_W;
    for (int x = 0; x < VIZ_W; x++) {
        uint16_t c = x < lvl ? (x < VIZ_W/2 ? GREEN : YELLOW) : BLACK;
        for (int y = 0; y < 4; y++) ((uint16_t *)s_viz)[y * VIZ_W + x] = c;
    }

    /* 3) frequency bars */
    int base_y = 6;
    int max_h = VIZ_H - base_y - WAVE_ROWS - 2;
    int bw = VIZ_W / NBANDS;
    for (int b = 0; b < NBANDS; b++) {
        int h = (int)(s_band[b] * max_h);
        if (h > max_h) h = max_h;
        int x0 = b * bw;
        uint16_t col = (b < NBANDS/3) ? GREEN : (b < 2*NBANDS/3 ? CYAN : MAGENTA);
        for (int y = 0; y < h; y++) {
            int yy = base_y + (max_h - 1 - y);
            for (int x = 0; x < bw - 1; x++) {
                ((uint16_t *)s_viz)[yy * VIZ_W + x0 + x] = col;
            }
        }
    }

    /* 4) scrolling waveform at the bottom of the band */
    int wy = VIZ_H - WAVE_ROWS;
    for (int x = 0; x < VIZ_W; x++) {
        int16_t s = samples[(x * n) / VIZ_W];
        int mid = wy + WAVE_ROWS/2;
        int off = (int)(((float)s / 32768.0f) * (WAVE_ROWS/2));
        int yy = mid + off;
        if (yy >= wy && yy < VIZ_H)
            ((uint16_t *)s_viz)[yy * VIZ_W + x] = YELLOW;
    }

    /* blit to panel */
    eh_lcd_blit_rect(VIZ_X, VIZ_Y, VIZ_W, VIZ_H, s_viz);
}
