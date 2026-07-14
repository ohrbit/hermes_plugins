/*
 * lcd_pet.c — petdex sprite cache + blit. DRAFT (no HW; LCD driver abstracted).
 *
 * Frames are cached in PSRAM (Stick-S3 has 8MB) so switching states is instant
 * and works offline. The actual SPI/LCD transport is a TODO that resolves to
 * the board's ILI9341/ST7789 driver at hardware arrival.
 */
#include "lcd_pet.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "eh_lcd_pet";
static bool       s_init = false;
static char       s_slug[32];
static uint8_t   *s_frames[EH_PET_STRETCH + 1];   /* one buffer per state */

esp_err_t eh_lcd_pet_init(void) {
    if (s_init) return ESP_OK;
    memset(s_frames, 0, sizeof(s_frames));
    memset(s_slug, 0, sizeof(s_slug));
    /* TODO(hw): init SPI + LCD controller (ILI9341/ST7789) for the board. */
    s_init = true;
    return ESP_OK;
}

void eh_lcd_pet_set_slug(const char *slug) {
    if (slug) strncpy(s_slug, slug, sizeof(s_slug) - 1);
}

esp_err_t eh_lcd_pet_set_frame(eh_pet_state_t state,
                               const uint8_t *rgb565, size_t len) {
    if (!s_init || state > EH_PET_STRETCH) return ESP_ERR_INVALID_ARG;
    if (len != EH_PET_FRAME_BYTES) return ESP_ERR_INVALID_SIZE;
    if (!s_frames[state]) s_frames[state] = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!s_frames[state]) return ESP_ERR_NO_MEM;
    memcpy(s_frames[state], rgb565, len);
    return ESP_OK;
}

esp_err_t eh_lcd_pet_render(eh_pet_state_t state, int pose_x, int pose_y) {
    if (!s_init) return ESP_ERR_INVALID_STATE;
    uint8_t *frame = s_frames[state];
    if (!frame) frame = s_frames[EH_PET_IDLE];   /* fallback */
    if (!frame) return ESP_ERR_NOT_FOUND;
    /* TODO(hw): blit `frame` to the LCD with (pose_x,pose_y) offset for the
     * IMU lean (clamp to +/- a few px so it stays on-panel). */
    (void)pose_x; (void)pose_y;
    return ESP_OK;
}

esp_err_t eh_lcd_pet_play_burst(const uint8_t *rgb565, size_t len, uint32_t ms) {
    if (!s_init || !rgb565) return ESP_ERR_INVALID_ARG;
    /* TODO(hw): display `rgb565` for `ms` then return to pet state. */
    (void)len; (void)ms;
    return ESP_OK;
}
