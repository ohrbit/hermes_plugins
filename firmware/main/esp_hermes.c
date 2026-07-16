/*
 * esp_hermes.c — shared protocol helpers (enum<->string, pin safety, URL build).
 * See esp_hermes.h for the protocol contract. DRAFT (no HW to test yet).
 */
#include "esp_hermes.h"
#include <string.h>

/* ---- pin safety (spec §8) ------------------------------------------------ */
static const int EH_BLOCKED[] = EH_BLOCKED_PINS_DEFAULT;

bool eh_pin_is_blocked(int pin) {
    for (int i = 0; i < EH_BLOCKED_PINS_COUNT; i++) {
        if (EH_BLOCKED[i] == pin) return true;
    }
    return false;
}

/* ---- enum <-> string ----------------------------------------------------- */
const char *eh_pet_state_str(eh_pet_state_t s) {
    switch (s) {
        case EH_PET_IDLE:    return "idle";
        case EH_PET_RUN:     return "run";
        case EH_PET_REVIEW:  return "review";
        case EH_PET_ERROR:   return "error";
        case EH_PET_DONE:    return "done";
        case EH_PET_TILT:    return "tilt";
        case EH_PET_SHAKE:   return "shake";
        case EH_PET_STRETCH: return "stretch";
        default:             return "idle";
    }
}
eh_pet_state_t eh_pet_state_from_str(const char *s) {
    if (!s) return EH_PET_IDLE;
    if (!strcmp(s, "run"))     return EH_PET_RUN;
    if (!strcmp(s, "review"))  return EH_PET_REVIEW;
    if (!strcmp(s, "error"))   return EH_PET_ERROR;
    if (!strcmp(s, "done"))    return EH_PET_DONE;
    if (!strcmp(s, "tilt"))    return EH_PET_TILT;
    if (!strcmp(s, "shake"))   return EH_PET_SHAKE;
    if (!strcmp(s, "stretch")) return EH_PET_STRETCH;
    return EH_PET_IDLE;
}

const char *eh_event_str(eh_event_t e) {
    switch (e) {
        case EH_EVT_TAP:   return "tap";
        case EH_EVT_SHAKE: return "shake";
        case EH_EVT_FLIP:  return "flip";
        case EH_EVT_STILL: return "still";
        default:           return "still";
    }
}
eh_event_t eh_event_from_str(const char *s) {
    if (!s) return EH_EVT_STILL;
    if (!strcmp(s, "tap"))   return EH_EVT_TAP;
    if (!strcmp(s, "shake")) return EH_EVT_SHAKE;
    if (!strcmp(s, "flip"))  return EH_EVT_FLIP;
    return EH_EVT_STILL;
}

const char *eh_tool_str(eh_tool_t t) {
    switch (t) {
        case EH_TOOL_GPIO_SET:  return "esp_gpio_set";
        case EH_TOOL_GPIO_READ: return "esp_gpio_read";
        case EH_TOOL_ADC_READ:  return "esp_adc_read";
        case EH_TOOL_PWM_SET:   return "esp_pwm_set";
        case EH_TOOL_I2C_READ:  return "esp_i2c_read";
        case EH_TOOL_I2C_WRITE: return "esp_i2c_write";
        case EH_TOOL_UART_SEND: return "esp_uart_send";
        case EH_TOOL_IMU_READ:  return "esp_imu_read";
        case EH_TOOL_MOTOR_SET: return "esp_motor_set";
        default:                return "unknown";
    }
}
eh_tool_t eh_tool_from_str(const char *s) {
    if (!s) return EH_TOOL_COUNT;
    if (!strcmp(s, "esp_gpio_set"))  return EH_TOOL_GPIO_SET;
    if (!strcmp(s, "esp_gpio_read")) return EH_TOOL_GPIO_READ;
    if (!strcmp(s, "esp_adc_read"))  return EH_TOOL_ADC_READ;
    if (!strcmp(s, "esp_pwm_set"))   return EH_TOOL_PWM_SET;
    if (!strcmp(s, "esp_i2c_read"))  return EH_TOOL_I2C_READ;
    if (!strcmp(s, "esp_i2c_write")) return EH_TOOL_I2C_WRITE;
    if (!strcmp(s, "esp_uart_send")) return EH_TOOL_UART_SEND;
    if (!strcmp(s, "esp_imu_read"))  return EH_TOOL_IMU_READ;
    if (!strcmp(s, "esp_motor_set")) return EH_TOOL_MOTOR_SET;
    return EH_TOOL_COUNT;
}

const char *eh_mode_str(eh_mode_t m) {
    return m == EH_MODE_VAD ? "vad" : "ptt";
}
eh_mode_t eh_mode_from_str(const char *s) {
    return (s && !strcmp(s, "vad")) ? EH_MODE_VAD : EH_MODE_PTT;
}

const char *eh_audio_fmt_str(eh_audio_format_t f) {
    switch (f) {
        case EH_AUDIO_OPUS:   return "opus";
        case EH_AUDIO_BINARY: return "binary";
        default:              return "pcm";
    }
}

/* ---- URL builder --------------------------------------------------------- */
int eh_build_ws_url(char *buf, size_t buflen,
                    const char *gateway_host,
                    const char *device_id,
                    const char *token) {
    if (!buf || !gateway_host || !device_id) return -1;
    const char *tok = token ? token : "";
    int n = snprintf(buf, buflen,
                     "wss://%s%s?device_id=%s&token=%s",
                     gateway_host, EH_WS_PATH, device_id, tok);
    if (n < 0 || (size_t)n >= buflen) return -1;
    return n;
}

/* ---- WS send helpers (called from client/app_main) ----------------------- */
#include "esp_websocket_client.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static esp_websocket_client_handle_t eh_ws = NULL;

void eh_send_capabilities(esp_websocket_client_handle_t ws) {
    eh_ws = ws;
    char msg[256];
    int n = snprintf(msg, sizeof(msg),
        "{\"type\":\"capabilities\",\"payload\":{"
        "\"imu\":true,\"pins\":[%d,%d,%d,%d,%d],"
        "\"i2c\":[%d,%d],\"audio\":true}}",
        EH_PIN_HY2_0, EH_PIN_HY2_1, EH_PIN_LCD_BL, EH_PIN_LCD_CS, EH_PIN_LCD_RS,
        EH_I2C_IMU_ADDR, EH_I2C_AUDIO_ADDR);
    esp_websocket_client_send_text(ws, msg, n, pdMS_TO_TICKS(1000));
}

void eh_send_event(esp_websocket_client_handle_t ws, eh_event_t evt) {
    const char *names[] = {"tap", "shake", "flip", "still"};
    char msg[64];
    int n = snprintf(msg, sizeof(msg),
        "{\"type\":\"event\",\"name\":\"%s\"}", names[evt]);
    esp_websocket_client_send_text(ws, msg, n, pdMS_TO_TICKS(1000));
}

void eh_send_ping(esp_websocket_client_handle_t ws) {
    esp_websocket_client_send_text(ws, "{\"type\":\"ping\"}", 14, pdMS_TO_TICKS(500));
}

void eh_heartbeat_task(void *pv) {
    esp_websocket_client_handle_t ws = (esp_websocket_client_handle_t)pv;
    for (;;) { eh_send_ping(ws); vTaskDelay(pdMS_TO_TICKS(EH_HEARTBEAT_TX_MS)); }
}

/* Gateway → ESP message dispatch (pet_state, tool_call, mode_ack, tui_line) */
void eh_handle_gateway_message(esp_websocket_client_handle_t ws,
                               const char *data, int len) {
    cJSON *j = cJSON_ParseWithLength(data, len);
    if (!j) return;
    const char *type = cJSON_GetObjectItem(j, "type")->valuestring;
    if (!strcmp(type, "pet_state")) {
        eh_pet_state_t s = eh_pet_state_from_str(
            cJSON_GetObjectItem(j, "state")->valuestring);
        lcd_pet_set_state(s);
    } else if (!strcmp(type, "tool_call")) {
        eh_tool_t t = (eh_tool_t)cJSON_GetObjectItem(j, "tool")->valueint;
        eh_tool_result_t r = io_tool_exec(t, cJSON_GetObjectItem(j, "params"));
        char resp[128];
        int n = snprintf(resp, sizeof(resp),
            "{\"type\":\"tool_result\",\"call_id\":%d,\"ok\":%s}",
            cJSON_GetObjectItem(j, "call_id")->valueint, r.ok ? "true" : "false");
        esp_websocket_client_send_text(ws, resp, n, pdMS_TO_TICKS(1000));
    } else if (!strcmp(type, "mode_ack")) {
        s_mode = (eh_mode_t)cJSON_GetObjectItem(j, "mode")->valueint;
    } else if (!strcmp(type, "tui_line")) {
        lcd_tui_push(cJSON_GetObjectItem(j, "text")->valuestring,
                     cJSON_GetObjectItem(j, "role")->valuestring);
    }
    cJSON_Delete(j);
}

/* Button: KEY1 = PTT (hold record), KEY2 = mode toggle */
void eh_button_task(void *pv) {
    esp_websocket_client_handle_t ws = (esp_websocket_client_handle_t)pv;
    gpio_set_direction(EH_PIN_BTN_A, GPIO_MODE_INPUT);
    gpio_set_direction(EH_PIN_BTN_B, GPIO_MODE_INPUT);
    gpio_pullup_en(EH_PIN_BTN_A);
    gpio_pullup_en(EH_PIN_BTN_B);
    bool a_prev = true, b_prev = true;
    for (;;) {
        bool a = gpio_get_level(EH_PIN_BTN_A);   /* active low */
        bool b = gpio_get_level(EH_PIN_BTN_B);
        if (!a && a_prev) {                       /* press KEY1 → start PTT */
            audio_capture_start(ws);
        } else if (a && !a_prev) {                /* release → stop + send */
            audio_capture_stop(ws);
        }
        if (!b && b_prev) {                       /* KEY2 → toggle mode */
            s_mode = (s_mode == EH_MODE_PTT) ? EH_MODE_VAD : EH_MODE_PTT;
            char m[32]; int n = snprintf(m, sizeof(m),
                "{\"type\":\"event\",\"name\":\"mode\",\"mode\":%d}", s_mode);
            esp_websocket_client_send_text(ws, m, n, pdMS_TO_TICKS(500));
        }
        a_prev = a; b_prev = b;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
