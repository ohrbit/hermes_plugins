/*
 * esp_hermes_client.c — main application entry point for the ESP-Hermes
 * channel firmware (M5Stack Stick S3). Thin gateway client: speaks the
 * esp_hermes WS protocol, captures audio, renders pet/TUI on LCD, handles
 * IMU events, and executes IO tool-calls.
 *
 * Pin map: see esp_hermes.h EH_PIN_* (Stick S3, SKU K150, verified m5-docs).
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_websocket_client.h"

#include "esp_hermes.h"
#include "nvs_config.h"
#include "capabilities.h"
#include "audio_capture.h"
#include "audio_play.h"
#include "lcd_pet.h"
#include "lcd_tui.h"
#include "imu_motion.h"
#include "io_tools.h"

static const char *TAG = "esp_hermes_client";

#define EH_WIFI_CONNECTED_BIT BIT0
static EventGroupHandle_t s_wifi_event_group;
static esp_websocket_client_handle_t s_ws = NULL;
eh_mode_t s_mode = EH_MODE_PTT;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
        esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "wifi disconnected, retrying");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
        xEventGroupSetBits(s_wifi_event_group, EH_WIFI_CONNECTED_BIT);
}

static void wifi_init_sta(const char *ssid, const char *pass)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t inst;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &inst));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &inst));
    wifi_config_t wcfg = {0};
    strncpy((char *)wcfg.sta.ssid, ssid, sizeof(wcfg.sta.ssid) - 1);
    strncpy((char *)wcfg.sta.password, pass, sizeof(wcfg.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "wifi_init_sta: connecting to %s", ssid);
}

static void ws_event_handler(void *h, esp_event_base_t base,
                             int32_t id, void *event_data)
{
    esp_websocket_event_data_t *d = (esp_websocket_event_data_t *)event_data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WS connected");
        eh_send_capabilities(s_ws);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WS disconnected");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (d->op_code == WS_TRANSPORT_OPCODES_BINARY)
            audio_play_write(d->data_ptr, d->data_len);
        else if (d->data_len > 0)
            eh_handle_gateway_message(s_ws, d->data_ptr, d->data_len);
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WS error");
        break;
    default: break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP-Hermes channel firmware starting (Stick S3)");
    ESP_ERROR_CHECK(nvs_flash_init());

    eh_config_t cfg;
    eh_config_load(&cfg);

    lcd_pet_init();
    lcd_tui_init();
    audio_capture_init();
    audio_play_init();
    imu_motion_init();
    io_tools_init();
    lcd_pet_set_state(EH_PET_IDLE);

    wifi_init_sta(cfg.wifi_ssid, cfg.wifi_pass);
    xEventGroupWaitBits(s_wifi_event_group, EH_WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    char uri[EH_URL_MAX];
    snprintf(uri, sizeof(uri), "wss://%s%s?device_id=%s&token=%s",
             cfg.gateway_host, EH_WS_PATH, cfg.device_id, cfg.token);
    esp_websocket_client_config_t ws_cfg = {
        .uri = uri, .reconnect_timeout_ms = EH_RECONNECT_MIN_MS,
        .task_stack = 8 * 1024,
    };
    s_ws = esp_websocket_client_init(&ws_cfg);
    ESP_ERROR_CHECK(esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY,
                                                   ws_event_handler, NULL));
    ESP_ERROR_CHECK(esp_websocket_client_start(s_ws));

    xTaskCreate(eh_heartbeat_task, "eh_hb", 4 * 1024, s_ws, 5, NULL);
    xTaskCreate(imu_motion_task, "imu_task", 6 * 1024, s_ws, 5, NULL);
    xTaskCreate(eh_button_task, "eh_btn", 4 * 1024, s_ws, 5, NULL);

    ESP_LOGI(TAG, "ESP-Hermes ready (mode=%s)",
             s_mode == EH_MODE_PTT ? "ptt" : "vad");
}
