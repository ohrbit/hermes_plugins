/*
 * io_tools.c — IO tool execution for ESP-Hermes channel.
 * Safety: every pin access checks eh_pin_is_blocked() + allowlist before use.
 */
#include "io_tools.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "io_tools";
static int s_allowed[EH_ALLOWED_PINS_COUNT];
static int s_allowed_n = 0;

void io_tools_init(void) {
    memcpy(s_allowed, (int[]){EH_ALLOWED_PINS_DEFAULT}, sizeof((int[]){EH_ALLOWED_PINS_DEFAULT}));
    s_allowed_n = EH_ALLOWED_PINS_COUNT;
    /* enable ADC + LEDC once */
    ledc_fade_func_install(0);
}

static bool pin_allowed(int pin) {
    if (eh_pin_is_blocked(pin)) return false;
    for (int i = 0; i < s_allowed_n; i++)
        if (s_allowed[i] == pin) return true;
    return false;   /* deny by default (spec §8) */
}

eh_tool_result_t io_tool_exec(eh_tool_t tool, const cJSON *params) {
    eh_tool_result_t r = { .ok = false, .value_i = 0, .value_s = NULL };
    switch (tool) {
    case EH_TOOL_GPIO_SET: {
        int pin = cJSON_GetObjectItem(params, "pin")->valueint;
        int st  = cJSON_GetObjectItem(params, "state")->valueint;
        if (!pin_allowed(pin)) { r.error = "pin blocked/denied"; break; }
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, st ? 1 : 0);
        r.ok = true;
        break;
    }
    case EH_TOOL_GPIO_READ: {
        int pin = cJSON_GetObjectItem(params, "pin")->valueint;
        if (!pin_allowed(pin)) { r.error = "pin blocked/denied"; break; }
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        r.value_i = gpio_get_level(pin);
        r.ok = true;
        break;
    }
    case EH_TOOL_ADC_READ: {
        int pin = cJSON_GetObjectItem(params, "pin")->valueint;
        if (!pin_allowed(pin)) { r.error = "pin blocked/denied"; break; }
        adc1_config_width(ADC_WIDTH_BIT_12);
        r.value_i = adc1_get_raw(pin);
        r.ok = true;
        break;
    }
    case EH_TOOL_PWM_SET: {
        int pin = cJSON_GetObjectItem(params, "pin")->valueint;
        int duty = cJSON_GetObjectItem(params, "duty")->valueint;
        if (!pin_allowed(pin)) { r.error = "pin blocked/denied"; break; }
        ledc_channel_config_t ch = {
            .gpio_num = pin, .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0, .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0, .duty = duty };
        ledc_channel_config(&ch);
        r.ok = true;
        break;
    }
    case EH_TOOL_I2C_READ: {
        int addr = cJSON_GetObjectItem(params, "addr")->valueint;
        int reg  = cJSON_GetObjectItem(params, "reg")->valueint;
        uint8_t v;
        if (i2c_master_write_read_device(EH_IMU_I2C_PORT, addr,
                (uint8_t[]){reg}, 1, &v, 1, pdMS_TO_TICKS(50)) == ESP_OK) {
            r.value_i = v; r.ok = true;
        } else r.error = "i2c read failed";
        break;
    }
    case EH_TOOL_I2C_WRITE: {
        int addr = cJSON_GetObjectItem(params, "addr")->valueint;
        int reg  = cJSON_GetObjectItem(params, "reg")->valueint;
        int val  = cJSON_GetObjectItem(params, "val")->valueint;
        uint8_t buf[2] = {reg, val};
        if (i2c_master_write_to_device(EH_IMU_I2C_PORT, addr, buf, 2,
                pdMS_TO_TICKS(50)) == ESP_OK) r.ok = true;
        else r.error = "i2c write failed";
        break;
    }
    case EH_TOOL_UART_SEND: {
        int port = cJSON_GetObjectItem(params, "port")->valueint;
        const char *data = cJSON_GetObjectItem(params, "data")->valuestring;
        uart_write_bytes(port, data, strlen(data));
        r.ok = true;
        break;
    }
    case EH_TOOL_IMU_READ: {
        float ax, ay, az;
        imu_read_accel(&ax, &ay, &az);
        r.value_s = malloc(64); sprintf(r.value_s, "%.2f,%.2f,%.2f", ax, ay, az);
        r.ok = true;
        break;
    }
    default:
        r.error = "unknown tool";
        break;
    }
    return r;
}
