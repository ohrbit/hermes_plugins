/*
 * lcd_tui.c — ring-buffer terminal scrollback. DRAFT (no HW; font + blit TODO).
 *
 * Layout (240x240, 8x8 font -> 30 cols x 20 rows region, status bar on top):
 *   row 0: status bar  "[PTT] ●conn  pet:idle"
 *   rows 1..18: scrollback (newest at bottom, auto-scroll, word-wrap, `…` trunc)
 *   row 19: pet-state glyph / hint line
 */
#include "lcd_tui.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "eh_lcd_tui";
static bool s_init = false;

/* ring buffer */
static char       s_lines[EH_TUI_ROWS][EH_TUI_LINE];
static int        s_head = 0;       /* next write index */
static int        s_count = 0;
static eh_mode_t  s_mode = EH_MODE_PTT;
static bool       s_connected = false;
static eh_pet_state_t s_pet = EH_PET_IDLE;

esp_err_t eh_lcd_tui_init(void) {
    if (s_init) return ESP_OK;
    memset(s_lines, 0, sizeof(s_lines));
    s_head = s_count = 0;
    s_init = true;
    return ESP_OK;
}

static void wrap_and_store(eh_tui_role_t role, const char *text) {
    /* prefix tag */
    char tagged[EH_TUI_LINE];
    const char *pfx = (role == EH_ROLE_USER) ? "U: " : "H: ";
    snprintf(tagged, sizeof(tagged), "%s", pfx);
    int base = (int)strlen(tagged);

    /* word-wrap into EH_TUI_COLS-wide lines */
    size_t len = strlen(text);
    int i = 0;
    char *dst = s_lines[s_head];
    strcpy(dst, tagged);
    int col = base;
    while (i < (int)len) {
        /* overflow: start a new ring line */
        if (col >= EH_TUI_COLS) {
            s_head = (s_head + 1) % EH_TUI_ROWS;
            if (s_count < EH_TUI_ROWS) s_count++;
            dst = s_lines[s_head];
            dst[0] = '\0';
            col = 0;
        }
        char c = text[i++];
        if (c == '\n') {
            s_head = (s_head + 1) % EH_TUI_ROWS;
            if (s_count < EH_TUI_ROWS) s_count++;
            dst = s_lines[s_head];
            dst[0] = '\0';
            col = 0;
            continue;
        }
        dst[col++] = c;
        dst[col] = '\0';
    }
    s_head = (s_head + 1) % EH_TUI_ROWS;
    if (s_count < EH_TUI_ROWS) s_count++;
}

void eh_lcd_tui_add(eh_tui_role_t role, const char *text) {
    if (!s_init || !text) return;
    wrap_and_store(role, text);
    eh_lcd_tui_redraw();
}

void eh_lcd_tui_redraw(void) {
    if (!s_init) return;
    /* TODO(hw): clear panel, draw status bar, then blit the ring buffer from
     * oldest (s_head - s_count) to newest. 8x8 monospace font. Accent color:
     * user lines one hue, agent lines another (RGB565). */
}

void eh_lcd_tui_set_mode(eh_mode_t mode) { s_mode = mode; eh_lcd_tui_redraw(); }
void eh_lcd_tui_set_connected(bool c)    { s_connected = c; eh_lcd_tui_redraw(); }
void eh_lcd_tui_set_pet(eh_pet_state_t st){ s_pet = st; eh_lcd_tui_redraw(); }
