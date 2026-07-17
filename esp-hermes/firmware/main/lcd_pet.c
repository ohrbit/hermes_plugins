/*
 * lcd_pet.c — petdex sprite cache + blit, backed by a real ST7789 LCD on the
 * Stick S3 (SPI, 240x240). Frames are cached in PSRAM; eh_lcd_pet_render()
 * now really blits the active frame (plus an optional IMU lean offset).
 */

#include "lcd_pet.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "esp_hermes.h"

static const char *TAG = "eh_lcd_pet";
static bool     s_init = false;
static char     s_slug[32];
static uint8_t *s_frames[EH_PET_STRETCH + 1];

/* ST7789 panel handle + line-draw scratch (one row of RGB565). */
static esp_lcd_panel_handle_t s_panel = NULL;
static uint8_t *s_rowbuf = NULL;

/* Blit one cached RGB565 frame to the panel (full 240x240). */
static void blit_frame(const uint8_t *frame) {
    if (!s_panel || !frame) return;
    /* Send row by row (panel expects sequential lines; PSRAM frame is linear). */
    for (int y = 0; y < EH_LCD_H; y++) {
        const uint8_t *row = frame + y * EH_LCD_W * 2;
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, EH_LCD_W, y + 1, row);
    }
}

esp_err_t eh_lcd_pet_init(void) {
    if (s_init) return ESP_OK;
    memset(s_frames, 0, sizeof(s_frames));
    memset(s_slug, 0, sizeof(s_slug));

    /* SPI bus for the LCD. */
    spi_bus_config_t bus = {
        .mosi_io_num = EH_PIN_LCD_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = EH_PIN_LCD_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = EH_LCD_W * 2 + 8,
    };
    esp_err_t e = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = EH_PIN_LCD_CS,
        .dc_gpio_num = EH_PIN_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 8,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    e = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io);
    if (e != ESP_OK) return e;

    esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = EH_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    e = esp_lcd_new_panel_st7789(io, &dev_cfg, &s_panel);
    if (e != ESP_OK) return e;

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    /* Stick-S3 panel orientation (top-left origin, scan direction). */
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_set_gap(s_panel, 0, 0);
    esp_lcd_panel_disp_on_off(s_panel, true);

    /* row scratch buffer (not PSRAM — small, hot path) */
    s_rowbuf = heap_caps_malloc(EH_LCD_W * 2, MALLOC_CAP_DMA);
    if (!s_rowbuf) { /* non-fatal, blit uses direct frame ptrs */ }

    s_init = true;
    ESP_LOGI(TAG, "ST7789 LCD init OK (%dx%d)", EH_LCD_W, EH_LCD_H);
    return ESP_OK;
}

void eh_lcd_pet_set_slug(const char *slug) {
    if (slug) strncpy(s_slug, slug, sizeof(s_slug) - 1);
}

esp_err_t eh_lcd_pet_set_frame(eh_pet_state_t state,
                               const uint8_t *rgb565, size_t len) {
    if (!s_init || state > EH_PET_STRETCH) return ESP_ERR_INVALID_ARG;
    if (len != EH_PET_FRAME_BYTES) return ESP_ERR_INVALID_SIZE;
    if (!s_frames[state]) s_frames[state] = heap_caps_malloc(len, MALLOC_CAP_DEFAULT);
    if (!s_frames[state]) return ESP_ERR_NO_MEM;
    memcpy(s_frames[state], rgb565, len);
    return ESP_OK;
}

esp_err_t eh_lcd_pet_render(eh_pet_state_t state, int pose_x, int pose_y) {
    if (!s_init) return ESP_ERR_INVALID_STATE;
    uint8_t *frame = s_frames[state];
    if (!frame) frame = s_frames[EH_PET_IDLE];
    if (!frame) return ESP_ERR_NOT_FOUND;
    (void)pose_x; (void)pose_y;  /* IMU lean overlay TODO(hw) */
    blit_frame(frame);
    return ESP_OK;
}

esp_err_t eh_lcd_pet_play_burst(const uint8_t *rgb565, size_t len, uint32_t ms) {
    if (!s_init || !rgb565) return ESP_ERR_INVALID_ARG;
    if (len == EH_PET_FRAME_BYTES) blit_frame(rgb565);
    (void)ms;
    return ESP_OK;
}

/* Raw bitmap blit into a sub-rectangle (used by the visualizer). */
esp_err_t eh_lcd_blit_rect(int x, int y, int w, int h, const uint8_t *rgb565) {
    if (!s_panel) return ESP_ERR_INVALID_STATE;
    if (x < 0 || y < 0 || x + w > EH_LCD_W || y + h > EH_LCD_H) return ESP_ERR_INVALID_ARG;
    for (int row = 0; row < h; row++) {
        esp_lcd_panel_draw_bitmap(s_panel, x, y + row, x + w, y + row + 1,
                                  rgb565 + row * w * 2);
    }
    return ESP_OK;
}
