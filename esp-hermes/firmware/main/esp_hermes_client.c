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
#include "lcd_viz.h"
#include "imu_motion.h"
#include "io_tools.h"
#include "wifi_manager.h"
#include "captive_dns.h"
#include "web_config.h"

static const char *TAG = "esp_hermes_client";

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

/* ---- Live audio visualizer task ---------------------------------------- */
static void eh_viz_task(void *arg)
{
    (void)arg;
    int16_t buf[256];
    for (;;) {
        if (eh_audio_capture_running()) {
            int n = eh_audio_capture_read(buf, 256);
            if (n > 0) eh_lcd_viz_update(buf, n);
        }
        vTaskDelay(pdMS_TO_TICKS(20));   /* ~50 fps cap */
    }
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
    eh_lcd_viz_init();
    ESP_ERROR_CHECK(eh_audio_capture_init());
    ESP_ERROR_CHECK(eh_audio_play_init());
    imu_motion_init();
    io_tools_init();
    eh_lcd_pet_render(EH_PET_IDLE, 0, 0);

    wifi_manager_init();

    bool have_wifi = (cfg.wifi_ssid[0] != '\0');
    if (!have_wifi) {
        /* Provisioning mode: SoftAP + internal web config server. */
        ESP_LOGI(TAG, "No WiFi configured -> provisioning mode (SoftAP + web UI)");
        wifi_manager_config_t wcfg = {
            .ap_ssid_prefix = "esp-hermes",
            .ap_behavior = "keep",
        };
        ESP_ERROR_CHECK(wifi_manager_start(&wcfg));
        ESP_ERROR_CHECK(eh_web_config_start());
        /* Captive portal: redirect all DNS to our UI. */
        captive_dns_config_t cdns = {
            .ap_netif = wifi_manager_get_ap_netif(),
            .redirect_ip = 0,            /* 0 -> use AP IP */
            .configure_dhcp_dns = true,
        };
        captive_dns_start(&cdns);
        /* Stay in provisioning until the user saves WiFi via the UI
         * (web_config POST applies STA config + restarts the chip). */
        ESP_LOGI(TAG, "Provisioning UI live at http://192.168.4.1");
        vTaskDelay(portMAX_DELAY);
        return;
    }

    /* STA mode: connect to configured WiFi, then bring up the web UI on the
     * STA interface too (so the user can reconfigure / switch backend). */
    wifi_manager_config_t wcfg = {
        .sta_ssid = cfg.wifi_ssid,
        .sta_password = cfg.wifi_pass[0] ? cfg.wifi_pass : NULL,
        .ap_ssid_prefix = "esp-hermes",
        .ap_behavior = "keep",
    };
    ESP_ERROR_CHECK(wifi_manager_start(&wcfg));
    ESP_ERROR_CHECK(wifi_manager_wait_connected(portMAX_DELAY));
    ESP_ERROR_CHECK(eh_web_config_start());

    /* Backend selection: Hermes Gateway vs local Claw. */
    if (strcmp(cfg.backend, "hermes") == 0) {
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
        ESP_LOGI(TAG, "Backend=hermes, gateway=%s", cfg.gateway_host);
    } else {
        /* Local Claw agent (esp-claw framework) — voice loop runs on-device.
         * WS gateway is not used; voice is handled by cap_hermes/local agent. */
        ESP_LOGI(TAG, "Backend=claw (local agent)");
        s_ws = NULL;
    }

    xTaskCreate(eh_heartbeat_task, "eh_hb", 4 * 1024, s_ws, 5, NULL);
    xTaskCreate(imu_motion_task, "imu_task", 6 * 1024, s_ws, 5, NULL);
    xTaskCreate(eh_button_task, "eh_btn", 4 * 1024, s_ws, 5, NULL);
    xTaskCreate(eh_viz_task, "eh_viz", 4 * 1024, NULL, 4, NULL);

    ESP_LOGI(TAG, "ESP-Hermes ready (mode=%s, backend=%s)",
             s_mode == EH_MODE_PTT ? "ptt" : "vad",
             cfg.backend[0] ? cfg.backend : "claw");
}
