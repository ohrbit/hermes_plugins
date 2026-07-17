/*
 * nvs_config.c — NVS-backed configuration. DRAFT (untested without HW).
 */
#include "nvs_config.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "eh_nvs";
static nvs_handle_t s_handle;

esp_err_t eh_nvs_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "erasing NVS and re-init");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;
    return nvs_open(EH_NVS_NS, NVS_READWRITE, &s_handle);
}

esp_err_t eh_nvs_load(eh_config_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;
    memset(cfg, 0, sizeof(*cfg));
    cfg->mode = EH_MODE_PTT;        /* default: push-to-talk */
    cfg->display_tui = false;
    cfg->muted = false;
    strncpy(cfg->gateway_host, "gw.local", sizeof(cfg->gateway_host) - 1);

    size_t len;
    if (nvs_get_str(s_handle, EH_NVS_KEY_DEV_ID, NULL, &len) == ESP_OK) {
        nvs_get_str(s_handle, EH_NVS_KEY_DEV_ID, cfg->device_id, &len);
    }
    if (nvs_get_str(s_handle, EH_NVS_KEY_TOKEN, NULL, &len) == ESP_OK) {
        nvs_get_str(s_handle, EH_NVS_KEY_TOKEN, cfg->token, &len);
    }
    if (nvs_get_str(s_handle, EH_NVS_KEY_HOST, NULL, &len) == ESP_OK) {
        nvs_get_str(s_handle, EH_NVS_KEY_HOST, cfg->gateway_host, &len);
    }
    if (nvs_get_str(s_handle, EH_NVS_KEY_PET, NULL, &len) == ESP_OK) {
        nvs_get_str(s_handle, EH_NVS_KEY_PET, cfg->pet_slug, &len);
    }
    int8_t mode = (int8_t)cfg->mode;
    nvs_get_i8(s_handle, EH_NVS_KEY_MODE, &mode);
    cfg->mode = (eh_mode_t)mode;
    uint8_t b = cfg->display_tui ? 1 : 0;
    nvs_get_u8(s_handle, EH_NVS_KEY_DISPLAY, &b);
    cfg->display_tui = b != 0;
    b = cfg->muted ? 1 : 0;
    nvs_get_u8(s_handle, EH_NVS_KEY_MUTE, &b);
    cfg->muted = b != 0;
    if (nvs_get_str(s_handle, EH_NVS_KEY_WIFI_SSID, NULL, &len) == ESP_OK) {
        nvs_get_str(s_handle, EH_NVS_KEY_WIFI_SSID, cfg->wifi_ssid, &len);
    }
    if (nvs_get_str(s_handle, EH_NVS_KEY_WIFI_PASS, NULL, &len) == ESP_OK) {
        nvs_get_str(s_handle, EH_NVS_KEY_WIFI_PASS, cfg->wifi_pass, &len);
    }
    if (nvs_get_str(s_handle, EH_NVS_KEY_BACKEND, NULL, &len) == ESP_OK) {
        nvs_get_str(s_handle, EH_NVS_KEY_BACKEND, cfg->backend, &len);
    } else {
        strncpy(cfg->backend, "claw", sizeof(cfg->backend) - 1);
    }
    return ESP_OK;
}

esp_err_t eh_nvs_save_mode(eh_mode_t mode) {
    esp_err_t e = nvs_set_i8(s_handle, EH_NVS_KEY_MODE, (int8_t)mode);
    return e == ESP_OK ? nvs_commit(s_handle) : e;
}
esp_err_t eh_nvs_save_display(bool tui) {
    esp_err_t e = nvs_set_u8(s_handle, EH_NVS_KEY_DISPLAY, tui ? 1 : 0);
    return e == ESP_OK ? nvs_commit(s_handle) : e;
}
esp_err_t eh_nvs_save_mute(bool muted) {
    esp_err_t e = nvs_set_u8(s_handle, EH_NVS_KEY_MUTE, muted ? 1 : 0);
    return e == ESP_OK ? nvs_commit(s_handle) : e;
}
esp_err_t eh_nvs_save_pet(const char *slug) {
    if (!slug) return ESP_ERR_INVALID_ARG;
    esp_err_t e = nvs_set_str(s_handle, EH_NVS_KEY_PET, slug);
    return e == ESP_OK ? nvs_commit(s_handle) : e;
}
esp_err_t eh_nvs_save_device(const char *device_id, const char *token,
                             const char *gateway_host) {
    esp_err_t e = ESP_OK;
    if (device_id) e = nvs_set_str(s_handle, EH_NVS_KEY_DEV_ID, device_id);
    if (e == ESP_OK && token) e = nvs_set_str(s_handle, EH_NVS_KEY_TOKEN, token);
    if (e == ESP_OK && gateway_host)
        e = nvs_set_str(s_handle, EH_NVS_KEY_HOST, gateway_host);
    return e == ESP_OK ? nvs_commit(s_handle) : e;
}

/* Persist the full config blob (used by the web config server). */
esp_err_t eh_nvs_save(const eh_config_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;
    esp_err_t e = ESP_OK;
    #define SAVE_STR(key, field) \
        do { if (cfg->field[0]) e = nvs_set_str(s_handle, key, cfg->field); \
             else e = nvs_set_str(s_handle, key, ""); \
             if (e != ESP_OK) return e; } while (0)
    SAVE_STR(EH_NVS_KEY_DEV_ID, device_id);
    SAVE_STR(EH_NVS_KEY_TOKEN, token);
    SAVE_STR(EH_NVS_KEY_HOST, gateway_host);
    SAVE_STR(EH_NVS_KEY_PET, pet_slug);
    SAVE_STR(EH_NVS_KEY_WIFI_SSID, wifi_ssid);
    SAVE_STR(EH_NVS_KEY_WIFI_PASS, wifi_pass);
    SAVE_STR(EH_NVS_KEY_BACKEND, backend);
    #undef SAVE_STR
    e = nvs_set_i8(s_handle, EH_NVS_KEY_MODE, (int8_t)cfg->mode);
    if (e == ESP_OK) e = nvs_set_u8(s_handle, EH_NVS_KEY_DISPLAY, cfg->display_tui ? 1 : 0);
    if (e == ESP_OK) e = nvs_set_u8(s_handle, EH_NVS_KEY_MUTE, cfg->muted ? 1 : 0);
    return e == ESP_OK ? nvs_commit(s_handle) : e;
}
