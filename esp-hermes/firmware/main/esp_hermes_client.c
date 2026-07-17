/*
 * esp_hermes_client.c — main application entry point for the ESP-Hermes
 * channel firmware (M5Stack Stick S3). Thin gateway client: speaks the
 * esp_hermes WS protocol, captures audio, renders pet/TUI on LCD, handles
 * IMU events, and executes IO tool-calls.
 *
 * DRAFT / WIRING NOTE: the WS message protocol handlers (capabilities send,
 * gateway-message parsing) and the heartbeat/button tasks are STUBBED here
 * so the firmware compiles and links against the v6 API surface. The real
 * protocol encoding is defined by esp_hermes.h (the contract) and must be
 * filled in once a gateway is available to validate against.
 * TODO(esp-hermes): implement eh_send_capabilities / eh_handle_gateway_message
 * with the real JSON encoding from esp_hermes.h, and the heartbeat/button tasks.
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

/* Placeholder WiFi credentials (TODO: read from NVS / provisioning). */
#define EH_WIFI_SSID "hermes-ap"
#define EH_WIFI_PASS "hermes-1234"

/* ---- STUB: send capabilities handshake over WS ------------------------- */
static void eh_send_capabilities(esp_websocket_client_handle_t ws)
{
    eh_capabilities_t caps;
    eh_capabilities_init(&caps, "stick-s3");
    char *json = eh_capabilities_to_json(&caps);
    if (json) {
        ESP_LOGI(TAG, "caps: %s", json);
        esp_websocket_client_send_text(ws, json, (int)strlen(json),
                                       pdMS_TO_TICKS(1000));
        free(json);
    }
}

/* ---- STUB: parse a gateway text message --------------------------------- */
static void eh_handle_gateway_message(esp_websocket_client_handle_t ws,
                                       const char *data, int len)
{
    /* TODO(esp-hermes): decode JSON per esp_hermes.h contract and dispatch
     * pet_state / audio / tool_call / mode_ack / tui_line. Ignored for now. */
    ESP_LOGD(TAG, "gw msg (%d bytes) ignored (stub)", len);
    (void)ws; (void)data; (void)len;
}

/* ---- STUB tasks -------------------------------------------------------- */
static void eh_heartbeat_task(void *arg)
{
    esp_websocket_client_handle_t ws = (esp_websocket_client_handle_t)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(EH_HEARTBEAT_TX_MS));
        if (ws) {
            const char *ping = "{\"type\":\"ping\"}";
            esp_websocket_client_send_text(ws, ping, (int)strlen(ping),
                                           pdMS_TO_TICKS(500));
        }
    }
}

static void eh_button_task(void *arg)
{
    /* TODO(esp-hermes): poll KEY1/KEY2 (G11/G12), drive PTT / mode toggle. */
    (void)arg;
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

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
            eh_audio_play_pcm((const int16_t *)d->data_ptr,
                              (int)(d->data_len / sizeof(int16_t)));
        else if (d->data_len > 0)
            eh_handle_gateway_message(s_ws, (const char *)d->data_ptr,
                                      (int)d->data_len);
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
    ESP_ERROR_CHECK(eh_nvs_init());

    eh_config_t cfg;
    eh_nvs_load(&cfg);

    ESP_ERROR_CHECK(eh_lcd_pet_init());
    ESP_ERROR_CHECK(eh_lcd_tui_init());
    ESP_ERROR_CHECK(eh_audio_capture_init());
    ESP_ERROR_CHECK(eh_audio_play_init());
    imu_motion_init();
    io_tools_init();
    eh_lcd_pet_render(EH_PET_IDLE, 0, 0);

    wifi_init_sta(EH_WIFI_SSID, EH_WIFI_PASS);
    xEventGroupWaitBits(s_wifi_event_group, EH_WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    char uri[512];
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
