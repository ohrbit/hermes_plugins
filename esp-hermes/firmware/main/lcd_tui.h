/*
 * lcd_tui.h — terminal-style scrollback on the ESP LCD (spec §6.5).
 *
 * Mirrors the desktop/Hermes TUI aesthetic: a compact monospace scrollback of
 * the live conversation, a status bar (mode + connection), and a pet-state side
 * glyph. Toggled with pet mode via gesture or gateway config (display_mode).
 */
#ifndef EH_LCD_TUI_H
#define EH_LCD_TUI_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_hermes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EH_TUI_COLS   30
#define EH_TUI_ROWS   20       /* ring-buffer lines */
#define EH_TUI_LINE   (EH_TUI_COLS + 1)

typedef enum { EH_ROLE_USER = 0, EH_ROLE_AGENT } eh_tui_role_t;

/* Initialize TUI layer (shares the LCD framebuffer with lcd_pet). */
esp_err_t eh_lcd_tui_init(void);

/* Append a conversation line (gateway pushes tui_line messages). */
void eh_lcd_tui_add(eh_tui_role_t role, const char *text);

/* Force a full redraw (call after add / status change / mode switch). */
void eh_lcd_tui_redraw(void);

/* Status bar state setters. */
void eh_lcd_tui_set_mode(eh_mode_t mode);
void eh_lcd_tui_set_connected(bool connected);
void eh_lcd_tui_set_pet(eh_pet_state_t state);   /* side glyph */

#ifdef __cplusplus
}
#endif
#endif /* EH_LCD_TUI_H */
