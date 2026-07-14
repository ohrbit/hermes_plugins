/*
 * nvs_config.h — persistent configuration (device_id, token, mode, gateway host).
 * Stored in NVS (spec §5, §7). Read once at boot, written on change.
 */
#ifndef EH_NVS_CONFIG_H
#define EH_NVS_CONFIG_H

#include <stdbool.h>
#include "esp_hermes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EH_NVS_NS          "esp_hermes"
#define EH_NVS_KEY_DEV_ID  "device_id"
#define EH_NVS_KEY_TOKEN   "token"
#define EH_NVS_KEY_MODE    "mode"        /* "ptt" | "vad" */
#define EH_NVS_KEY_HOST    "gw_host"
#define EH_NVS_KEY_PET     "pet_slug"
#define EH_NVS_KEY_DISPLAY "display"     /* "pet" | "tui" */
#define EH_NVS_KEY_MUTE    "mute"

/* Runtime config blob (loaded from NVS at boot). */
typedef struct {
    char device_id[EH_DEVICE_ID_MAX];
    char token[EH_TOKEN_MAX];
    char gateway_host[128];
    char pet_slug[32];
    eh_mode_t     mode;
    bool          display_tui;   /* false -> pet mode, true -> TUI mode */
    bool          muted;         /* audio cues off */
} eh_config_t;

/* Initialize NVS flash partition. Call before load. */
esp_err_t eh_nvs_init(void);

/* Load full config from NVS into *cfg. Falls back to defaults if absent. */
esp_err_t eh_nvs_load(eh_config_t *cfg);

/* Persist one field (caller sets the field in *cfg first). */
esp_err_t eh_nvs_save_mode(eh_mode_t mode);
esp_err_t eh_nvs_save_display(bool tui);
esp_err_t eh_nvs_save_mute(bool muted);
esp_err_t eh_nvs_save_pet(const char *slug);
esp_err_t eh_nvs_save_device(const char *device_id, const char *token,
                             const char *gateway_host);

#ifdef __cplusplus
}
#endif
#endif /* EH_NVS_CONFIG_H */
