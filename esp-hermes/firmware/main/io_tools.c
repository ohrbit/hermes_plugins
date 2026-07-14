/*
 * io_tools.c — ESP-side IO tool execution. DRAFT (no HW; drivers abstracted).
 *
 * Matches gateway tool schemas (tools/esp_io.py §3.4):
 *   esp_gpio_set  {pin, state:"HIGH"|"LOW"}
 *   esp_gpio_read {pin}                       -> level (0/1)
 *   esp_adc_read  {pin, atten}                -> mV
 *   esp_pwm_set   {pin, duty, freq}           -> ledc
 *   esp_i2c_read  {addr, reg, len}            -> bytes (hex string)
 *   esp_i2c_write {addr, reg, data:[...]}
 *   esp_uart_send {data}                      -> echo len
 *   esp_imu_read  {axis:"all"|"accel"|"gyro"} -> vector
 *   esp_motor_set {angle|speed}               -> Phase 5 stub
 *
 * Safety (spec §8): always-blocked pins refused unconditionally; allowed-pin
 * list enforced (least privilege); rate-limited gpio/pwm to prevent strobe /
 * burn-out loops. See eh_io_set_allowed_pins().
 */
#include "io_tools.h"
#include <string.h>
#include "esp_hermes.h"
#include "capabilities.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/adc.h"
#include "driver/i2c.h"
#include "imu_motion.h"

static const char *TAG = "eh_io";

/* runtime allowlist */
static int  s_allowed[EH_MAX_PINS];
static int  s_allowed_n = 0;

/* rate limit counters (spec §8) */
static int64_t s_last_gpio_ms = 0;
static int     s_gpio_window = 0;
static int64_t s_last_pwm_ms = 0;
static int     s_pwm_window = 0;
#define EH_GPIO_RL_WINDOW_MS 1000
#define EH_GPIO_RL_MAX 5
#define EH_PWM_RL_WINDOW_MS 1000
#define EH_PWM_RL_MAX 5

void eh_io_set_allowed_pins(const int *pins, int n) {
    s_allowed_n = 0;
    if (!pins) return;
    for (int i = 0; i < n && s_allowed_n < EH_MAX_PINS; i++) {
        if (!eh_pin_is_blocked(pins[i]))
            s_allowed[s_allowed_n++] = pins[i];
    }
}

static bool pin_allowed(int pin) {
    if (eh_pin_is_blocked(pin)) return false;          /* never */
    for (int i = 0; i < s_allowed_n; i++)
        if (s_allowed[i] == pin) return true;
    return false;                                       /* deny by default */
}

static bool rl_ok(int64_t now_ms, int64_t *last, int *win, int max) {
    if (now_ms - *last > EH_GPIO_RL_WINDOW_MS) { *last = now_ms; *win = 0; }
    if (*win >= max) return false;
    (*win)++;
    return true;
}

static void set_result(eh_tool_result_t *r, bool ok, int32_t v, const char *err) {
    r->ok = ok; r->value = v;
    if (err) strncpy(r->error, err, sizeof(r->error) - 1);
    else r->error[0] = '\0';
}

eh_tool_result_t eh_io_exec(eh_tool_t tool, const cJSON *params) {
    eh_tool_result_t r;
    memset(&r, 0, sizeof(r));
    int64_t now = esp_timer_get_time() / 1000;

    switch (tool) {
        case EH_TOOL_GPIO_SET: {
            int pin = eh_json_int(params, "pin", -1);
            if (!pin_allowed(pin)) { set_result(&r, false, 0, "pin_denied"); break; }
            if (!rl_ok(now, &s_last_gpio_ms, &s_gpio_window, EH_GPIO_RL_MAX)) {
                set_result(&r, false, 0, "rate_limited"); break;
            }
            const char *st = eh_json_str(params, "state");
            int lvl = (st && !strcmp(st, "HIGH")) ? 1 : 0;
            gpio_set_level(pin, lvl);
            set_result(&r, true, lvl, NULL);
            break;
        }
        case EH_TOOL_GPIO_READ: {
            int pin = eh_json_int(params, "pin", -1);
            if (!pin_allowed(pin)) { set_result(&r, false, 0, "pin_denied"); break; }
            set_result(&r, true, gpio_get_level(pin), NULL);
            break;
        }
        case EH_TOOL_ADC_READ: {
            int pin = eh_json_int(params, "pin", -1);
            if (!pin_allowed(pin)) { set_result(&r, false, 0, "pin_denied"); break; }
            /* TODO(hw): map pin->adc_channel, configure atten, read mV. */
            int mV = 0;  /* placeholder */
            set_result(&r, true, mV, NULL);
            break;
        }
        case EH_TOOL_PWM_SET: {
            int pin = eh_json_int(params, "pin", -1);
            if (!pin_allowed(pin)) { set_result(&r, false, 0, "pin_denied"); break; }
            if (!rl_ok(now, &s_last_pwm_ms, &s_pwm_window, EH_PWM_RL_MAX)) {
                set_result(&r, false, 0, "rate_limited"); break;
            }
            int duty = eh_json_int(params, "duty", 0);
            int freq = eh_json_int(params, "freq", 5000);
            /* TODO(hw): ledc_setup + ledc_set_duty on pin. */
            (void)duty; (void)freq;
            set_result(&r, true, duty, NULL);
            break;
        }
        case EH_TOOL_I2C_READ: {
            uint8_t addr = (uint8_t)eh_json_int(params, "addr", 0);
            int reg = eh_json_int(params, "reg", 0);
            int len = eh_json_int(params, "len", 1);
            /* TODO(hw): i2c_master_read_from_device. */
            (void)addr; (void)reg; (void)len;
            set_result(&r, true, 0, NULL);
            break;
        }
        case EH_TOOL_I2C_WRITE: {
            uint8_t addr = (uint8_t)eh_json_int(params, "addr", 0);
            int reg = eh_json_int(params, "reg", 0);
            /* TODO(hw): read data[] array, i2c_master_write_to_device. */
            (void)addr; (void)reg;
            set_result(&r, true, 0, NULL);
            break;
        }
        case EH_TOOL_UART_SEND: {
            const char *data = eh_json_str(params, "data");
            if (!data) { set_result(&r, false, 0, "no_data"); break; }
            /* TODO(hw): uart_write_bytes on passthrough UART. */
            set_result(&r, true, (int32_t)strlen(data), NULL);
            break;
        }
        case EH_TOOL_IMU_READ: {
            const char *axis = eh_json_str(params, "axis");
            eh_imu_vec_t v;
            eh_imu_read(&v);
            if (axis && !strcmp(axis, "accel"))
                set_result(&r, true, (int32_t)(v.ax * 1000), NULL);
            else if (axis && !strcmp(axis, "gyro"))
                set_result(&r, true, (int32_t)(v.gx * 1000), NULL);
            else
                set_result(&r, true,
                           (int32_t)(v.ax * 1000 + v.gx), NULL);  /* 'all' */
            break;
        }
        case EH_TOOL_MOTOR_SET: {
            /* Phase 5 stub (spec §5, §9). Motor not attached in Phase 1-4. */
            set_result(&r, false, 0, "not_implemented");
            break;
        }
        default:
            set_result(&r, false, 0, "unknown_tool");
            break;
    }
    return r;
}

char *eh_io_handle_call(const cJSON *msg) {
    if (!msg) return NULL;
    const char *tool_s = eh_json_str(msg, "tool");
    const char *call_id = eh_json_str(msg, "call_id");
    if (!tool_s || !call_id) return NULL;
    eh_tool_t t = eh_tool_from_str(tool_s);
    if (t == EH_TOOL_COUNT) {
        cJSON *out = cJSON_CreateObject();
        cJSON_AddStringToObject(out, "type", "tool_result");
        cJSON_AddStringToObject(out, "call_id", call_id);
        cJSON_AddBoolToObject(out, "ok", false);
        cJSON_AddStringToObject(out, "error", "unknown_tool");
        char *s = cJSON_PrintUnformatted(out);
        cJSON_Delete(out);
        return s;
    }
    cJSON *params = cJSON_GetObjectItemCaseSensitive(msg, "params");
    if (!params) params = cJSON_CreateObject();
    eh_tool_result_t r = eh_io_exec(t, params);

    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "type", "tool_result");
    cJSON_AddStringToObject(out, "call_id", call_id);
    cJSON_AddBoolToObject(out, "ok", r.ok);
    cJSON_AddNumberToObject(out, "value", r.value);
    if (!r.ok) cJSON_AddStringToObject(out, "error", r.error);
    char *s = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    return s;
}
