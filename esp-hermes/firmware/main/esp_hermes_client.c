/*
 * esp_hermes_client.c — main firmware entry + WebSocket client (spec §5, §3).
 *
 * This is the orchestrator that was forked from ESP-Claw edge_agent with the
 * LLM client STRIPPED. The ESP never calls OpenAI/Anthropic directly — it is a
 * thin gateway client. Hermes (gateway) is the brain.
 *
 * Responsibilities:
 *   - wifi + NVS config load
 *   - WS connect to gateway (wss://.../api/esp_hermes/ws?device_id=&token=)
 *   - capabilities handshake first (spec §3.1)
 *   - inbound dispatch: pet_state/video/audio/tool_call/mode_ack/tui_line/pong
 *   - uplink: audio (PTT + VAD), imu/button events, imu vectors, tool_result
 *   - heartbeat (ping every 30s; drop if no pong 90s) + auto-reconnect w/ backoff
 *   - modes PTT / VAD (spec §7)
 *   - audio cues on state change (spec §16); degrade gracefully offline (§12)
 *
 * DRAFT: not compiled/flashed (no hardware). Driver init calls are real ESP-IDF
 * APIs; TODO markers flag board-specific tuning. The WebSocket transport uses
 * the esp-websocket-client component which mirrors the gateway hub's frame
 * handling (JSON text + binary audio).
 */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_websocket_client.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "esp_hermes.h"
#include "nvs_config.h"
#include "capabilities.h"
#include "audio_capture.h"
#include "audio_play.h"
#include "audio_codec.h"
#include "lcd_pet.h"
#include "lcd_tui.h"
#include "imu_motion.h"
#include "io_tools.h"

static const char *TAG = "eh_main";

/* ---- runtime state ---- */
static eh_config_t      s_cfg;
static eh_capabilities_t s_caps;
static esp_websocket_client_handle_t s_ws = NULL;
static EventGroupHandle_t s_wifi_evt;
static bool s_connected = false;
static int64_t s_last_pong_ms = 0;

/* forward decls */
static void ws_event_handler(void *handler_args, esp_event_base_t base,
                             int32_t event_id, void *event_data);
static void send_capabilities(void);
static void send_audio_frame(const int16_t *pcm, int samples);
static void send_event(eh_event_t e);
static void send_imu(const eh_imu_vec_t *v);
static void handle_inbound(const char *json);
static void pet_state_changed(eh_pet_state_t s);
static void enter_mode(eh_mode_t m);
static void reconnect_loop(void);

/* =========================================================================
 * WiFi
 * ======================================================================= */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        s_connected = false;
        xEventGroupClearBits(s_wifi_evt, WIFI_CONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_evt, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void) {
    s_wifi_evt = xEventGroupCreate();
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
    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, CONFIG_ESP_WIFI_SSID, sizeof(wc.sta.ssid));
    strncpy((char *)wc.sta.password, CONFIG_ESP_WIFI_PASSWORD,
            sizeof(wc.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* wait for IP (or give up after 15s; firmware still runs offline, §12) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_evt,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(15000));
    if (bits & WIFI_CONNECTED_BIT) ESP_LOGI(TAG, "wifi connected");
    else ESP_LOGW(TAG, "wifi not connected — running offline (degraded)");
}

/* =========================================================================
 * WebSocket client
 * ======================================================================= */
static void ws_event_handler(void *handler_args, esp_event_base_t base,
                             int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *d = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WS connected");
            s_connected = true;
            s_last_pong_ms = esp_timer_get_time() / 1000;
            send_capabilities();
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WS disconnected");
            s_connected = false;
            break;
        case WEBSOCKET_EVENT_DATA: {
            if (d->op_code == 0x2) {            /* binary => raw audio (TTS) */
                int16_t pcm[512];
                int n = eh_audio_decode(eh_audio_negotiated_format(),
                                        d->data_ptr, d->data_len, pcm, 512);
                if (n > 0) eh_audio_play_pcm(pcm, n);
            } else if (d->data_len > 0) {       /* text JSON */
                char buf[d->data_len + 1];
                memcpy(buf, d->data_ptr, d->data_len);
                buf[d->data_len] = '\0';
                handle_inbound(buf);
            }
            break;
        }
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WS error");
            break;
        default:
            break;
    }
}

static void ws_start(void) {
    char uri[EH_URL_MAX];
    eh_build_ws_url(uri, sizeof(uri), s_cfg.gateway_host,
                   s_cfg.device_id, s_cfg.token);
    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .reconnect_timeout_ms = 2000,
        .disable_auto_reconnect = false,
    };
    s_ws = esp_websocket_client_init(&cfg);
    ESP_ERROR_CHECK(esp_websocket_client_register_events(
        s_ws, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL));
    ESP_ERROR_CHECK(esp_websocket_client_start(s_ws));
}

/* ---- outbound helpers ---- */
static void ws_send_str(const char *s) {
    if (s_ws && s_connected)
        esp_websocket_client_send_text(s_ws, s, strlen(s),
                                       pdMS_TO_TICKS(1000));
}

static void send_capabilities(void) {
    char *json = eh_capabilities_to_json(&s_caps);
    if (json) { ws_send_str(json); free(json); }
}

static void send_audio_frame(const int16_t *pcm, int samples) {
    uint8_t enc[1024];
    int n = eh_audio_encode(eh_audio_negotiated_format(), pcm, samples,
                            enc, sizeof(enc));
    if (n <= 0) return;
    /* send as binary frame (gateway accepts raw audio, §3.3) */
    if (s_ws && s_connected)
        esp_websocket_client_send_bin(s_ws, (char *)enc, n, pdMS_TO_TICKS(1000));
}

static void send_event(eh_event_t e) {
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "type", "event");
    cJSON_AddStringToObject(m, "name", eh_event_str(e));
    cJSON_AddNumberToObject(m, "ts", (int32_t)(esp_timer_get_time() / 1000));
    char *s = cJSON_PrintUnformatted(m);
    cJSON_Delete(m);
    if (s) { ws_send_str(s); free(s); }
}

static void send_imu(const eh_imu_vec_t *v) {
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "type", "imu");
    cJSON *a = cJSON_CreateArray();
    cJSON_AddItemToArray(a, cJSON_CreateNumber(v->ax));
    cJSON_AddItemToArray(a, cJSON_CreateNumber(v->ay));
    cJSON_AddItemToArray(a, cJSON_CreateNumber(v->az));
    cJSON_AddItemToObject(m, "accel", a);
    cJSON *g = cJSON_CreateArray();
    cJSON_AddItemToArray(g, cJSON_CreateNumber(v->gx));
    cJSON_AddItemToArray(g, cJSON_CreateNumber(v->gy));
    cJSON_AddItemToArray(g, cJSON_CreateNumber(v->gz));
    cJSON_AddItemToObject(m, "gyro", g);
    char *s = cJSON_PrintUnformatted(m);
    cJSON_Delete(m);
    if (s) { ws_send_str(s); free(s); }
}

/* ---- inbound dispatch ---- */
static void handle_inbound(const char *json) {
    cJSON *m = cJSON_Parse(json);
    if (!m) return;
    const char *type = eh_json_str(m, "type");
    if (!type) { cJSON_Delete(m); return; }

    if (!strcmp(type, "pet_state")) {
        eh_pet_state_t st = eh_pet_state_from_str(eh_json_str(m, "state"));
        pet_state_changed(st);
    } else if (!strcmp(type, "video")) {
        /* optional burst (§6). TODO(hw): decode + play gif/mjpeg. */
    } else if (!strcmp(type, "audio")) {
        /* gateway-sent TTS as JSON (base64) path; binary handled in event cb */
        const char *b64 = eh_json_str(m, "data");
        if (b64) { /* TODO(hw): base64 decode -> eh_audio_decode -> play */ }
    } else if (!strcmp(type, "tool_call")) {
        char *res = eh_io_handle_call(m);
        if (res) { ws_send_str(res); free(res); }
    } else if (!strcmp(type, "mode_ack")) {
        enter_mode(eh_mode_from_str(eh_json_str(m, "mode")));
    } else if (!strcmp(type, "tui_line")) {
        const char *role = eh_json_str(m, "role");
        const char *text = eh_json_str(m, "text");
        if (text)
            eh_lcd_tui_add(role && !strcmp(role, "agent") ? EH_ROLE_AGENT
                                                          : EH_ROLE_USER, text);
    } else if (!strcmp(type, "pong")) {
        s_last_pong_ms = esp_timer_get_time() / 1000;
    }
    cJSON_Delete(m);
}

static void pet_state_changed(eh_pet_state_t s) {
    /* render pet (or TUI side glyph) + play local cue (§16) */
    int px = 0, py = 0;
    if (!s_cfg.muted) {
        eh_audio_cue(s);
        eh_lcd_tui_set_pet(s);
    }
    eh_imu_pose(&px, &py);
    if (s_cfg.display_tui)
        eh_lcd_tui_set_pet(s);
    else
        eh_lcd_pet_render(s, px, py);
}

static void enter_mode(eh_mode_t m) {
    s_cfg.mode = m;
    eh_nvs_save_mode(m);
    eh_lcd_tui_set_mode(m);
    if (!s_cfg.muted) eh_audio_play_tone(500, 40, 0.25);  /* mode click */
    if (m == EH_MODE_VAD) eh_audio_capture_start();
    else eh_audio_capture_stop();
}

/* =========================================================================
 * Main loop: audio capture, IMU events, heartbeat, reconnect (spec §12)
 * ======================================================================= */
static void reconnect_loop(void) { /* auto-reconnect handled by ws client */ }

static void main_task(void *arg) {
    (void)arg;
    int16_t pcm[256];
    int64_t last_hb = 0, last_imu_push = 0;

    while (1) {
        int64_t now = esp_timer_get_time() / 1000;

        /* ---- audio uplink ---- */
        if (s_connected) {
            if (s_cfg.mode == EH_MODE_PTT) {
                /* PTT: capture started/stopped by button task (not here) */
            } else { /* VAD */
                if (eh_audio_vad_active()) {
                    int n = eh_audio_capture_read(pcm, 256);
                    if (n > 0) send_audio_frame(pcm, n);
                }
            }
        }

        /* ---- IMU events (debounced) ---- */
        eh_event_t ev = eh_imu_poll_events();
        if (ev != EH_EVT_STILL) {
            send_event(ev);
            if (ev == EH_EVT_SHAKE) enter_mode(s_cfg.mode == EH_MODE_VAD
                                                      ? EH_MODE_PTT : EH_MODE_VAD);
        }
        /* periodic IMU vector push (polled mode) */
        if (s_connected && now - last_imu_push > 1000) {
            eh_imu_vec_t v; eh_imu_read(&v); send_imu(&v);
            last_imu_push = now;
        }

        /* ---- heartbeat ---- */
        if (s_connected && now - last_hb > EH_HEARTBEAT_TX_MS) {
            cJSON *p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "type", "ping");
            char *s = cJSON_PrintUnformatted(p);
            cJSON_Delete(p);
            if (s) { ws_send_str(s); free(s); }
            last_hb = now;
        }
        /* gateway liveness: drop to idle if no pong (§12) */
        if (s_connected && now - s_last_pong_ms > EH_HEARTBEAT_RX_MS) {
            ESP_LOGW(TAG, "heartbeat timeout — entering idle, will reconnect");
            s_connected = false;
            if (!s_cfg.muted) eh_lcd_pet_render(EH_PET_IDLE, 0, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* =========================================================================
 * app_main
 * ======================================================================= */
void app_main(void) {
    ESP_ERROR_CHECK(eh_nvs_init());
    ESP_ERROR_CHECK(eh_nvs_load(&s_cfg));

    /* board hardware init (DRAFT: drivers real, tuning TODO) */
    ESP_ERROR_CHECK(eh_audio_capture_init());
    ESP_ERROR_CHECK(eh_audio_play_init());
    ESP_ERROR_CHECK(eh_lcd_pet_init());
    ESP_ERROR_CHECK(eh_lcd_tui_init());
    ESP_ERROR_CHECK(eh_imu_init());

    /* capabilities from board defaults + NVS allowlist */
    eh_capabilities_init(&s_caps, "stick-s3");
    eh_io_set_allowed_pins(s_caps.pins, s_caps.pin_count);

    /* default display + pet */
    if (s_cfg.pet_slug[0]) eh_lcd_pet_set_slug(s_cfg.pet_slug);
    eh_lcd_tui_set_mode(s_cfg.mode);
    eh_lcd_tui_set_connected(false);

    wifi_init();
    ws_start();

    /* button / PTT task would be spawned here (hold=record, release=upload).
     * For the draft we rely on VAD + shake-to-toggle in main_task. */
    xTaskCreate(main_task, "eh_main", 8192, NULL, 5, NULL);
}
