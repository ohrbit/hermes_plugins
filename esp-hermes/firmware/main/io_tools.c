/*
 * io_tools.c — IO tool execution for ESP-Hermes channel.
 * Safety: every pin access checks eh_pin_is_blocked() + allowlist before use.
 * ESP-IDF v5+: uses unified esp_adc API (legacy adc1_get_raw removed).
 */
#include "io_tools.h"
#include <string.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "io_tools";
static int s_allowed[EH_ALLOWED_PINS_COUNT];
static int s_allowed_n = 0;

void io_tools_init(void) {
    int def[] = EH_ALLOWED_PINS_DEFAULT;
    memcpy(s_allowed, def, sizeof(def));
    s_allowed_n = EH_ALLOWED_PINS_COUNT;
    /* NOTE: ledc_fade_func_install() panics on this S3 unit (v6 HAL); fade is
       unused for now, so skip it. Can revisit if we add LED breathing. */
}

static bool pin_allowed(int pin) {
    if (eh_pin_is_blocked(pin)) return false;
    for (int i = 0; i < s_allowed_n; i++)
        if (s_allowed[i] == pin) return true;
    return false;   /* deny by default (spec §8) */
}

/* ESP32-S3 ADC1 channel for a GPIO pin (TRM: GPIO1=CH0 .. GPIO10=CH9). */
static int gpio_to_adc1_ch(int pin) {
    if (pin >= 1 && pin <= 10) return pin - 1;
    return -1;
}

eh_tool_result_t io_tool_exec(eh_tool_t tool, const cJSON *params) {
    eh_tool_result_t r = { .ok = false, .value = 0, .value_str[0] = '\0' };
    ESP_LOGD(TAG, "tool_exec %d", (int)tool);
    switch (tool) {
    case EH_TOOL_GPIO_SET: {
        int pin = cJSON_GetObjectItem(params, "pin")->valueint;
        int st  = cJSON_GetObjectItem(params, "state")->valueint;
        if (!pin_allowed(pin)) { strncpy(r.error, "pin blocked/denied", sizeof(r.error)-1); r.error[sizeof(r.error)-1]='\0'; break; }
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, st ? 1 : 0);
        r.ok = true;
        break;
    }
    case EH_TOOL_GPIO_READ: {
        int pin = cJSON_GetObjectItem(params, "pin")->valueint;
        if (!pin_allowed(pin)) { strncpy(r.error, "pin blocked/denied", sizeof(r.error)-1); r.error[sizeof(r.error)-1]='\0'; break; }
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        r.value = gpio_get_level(pin);
        r.ok = true;
        break;
    }
    case EH_TOOL_ADC_READ: {
        int pin = cJSON_GetObjectItem(params, "pin")->valueint;
        if (!pin_allowed(pin)) { strncpy(r.error, "pin blocked/denied", sizeof(r.error)-1); r.error[sizeof(r.error)-1]='\0'; break; }
        int ch = gpio_to_adc1_ch(pin);
        if (ch < 0) { strncpy(r.error, "pin not on ADC1", sizeof(r.error)-1); r.error[sizeof(r.error)-1]='\0'; break; }
        adc_oneshot_unit_handle_t unit;
        adc_oneshot_unit_init_cfg_t u = { .unit_id = ADC_UNIT_1 };
        if (adc_oneshot_new_unit(&u, &unit) != ESP_OK) { strncpy(r.error, "adc init", sizeof(r.error)-1); r.error[sizeof(r.error)-1]='\0'; break; }
        adc_oneshot_chan_cfg_t c = { .atten = ADC_ATTEN_DB_12,
                                      .bitwidth = ADC_BITWIDTH_12 };
        if (adc_oneshot_config_channel(unit, ch, &c) != ESP_OK) {
            strncpy(r.error, "adc cfg", sizeof(r.error)-1); r.error[sizeof(r.error)-1]='\0';
            adc_oneshot_del_unit(unit); break;
        }
        int raw = 0;
        if (adc_oneshot_read(unit, ch, &raw) == ESP_OK) {
            r.value = raw; r.ok = true;
        } else { strncpy(r.error, "adc read", sizeof(r.error)-1); r.error[sizeof(r.error)-1]='\0'; }
        adc_oneshot_del_unit(unit);
        break;
    }
    case EH_TOOL_PWM_SET: {
        int pin = cJSON_GetObjectItem(params, "pin")->valueint;
        int duty = cJSON_GetObjectItem(params, "duty")->valueint;
        if (!pin_allowed(pin)) { strncpy(r.error, "pin blocked/denied", sizeof(r.error)-1); r.error[sizeof(r.error)-1]='\0'; break; }
        ledc_timer_config_t t = {
            .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_13_BIT,
            .timer_num = LEDC_TIMER_0, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
        ledc_timer_config(&t);
        ledc_channel_config_t ch = {
            .gpio_num = pin, .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0, .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0, .duty = duty, .hpoint = 0 };
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
            r.value = v; r.ok = true;
        } else { strncpy(r.error, "i2c read failed", sizeof(r.error)-1); r.error[sizeof(r.error)-1]='\0'; }
        break;
    }
    case EH_TOOL_I2C_WRITE: {
        int addr = cJSON_GetObjectItem(params, "addr")->valueint;
        int reg  = cJSON_GetObjectItem(params, "reg")->valueint;
        int val  = cJSON_GetObjectItem(params, "val")->valueint;
        uint8_t buf[2] = {reg, val};
        if (i2c_master_write_to_device(EH_IMU_I2C_PORT, addr, buf, 2,
                pdMS_TO_TICKS(50)) == ESP_OK) r.ok = true;
        else { strncpy(r.error, "i2c write failed", sizeof(r.error)-1); r.error[sizeof(r.error)-1]='\0'; }
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
        snprintf(r.value_str, sizeof(r.value_str), "%.2f,%.2f,%.2f", ax, ay, az);
        r.ok = true;
        break;
    }
    default:
        strncpy(r.error, "unknown tool", sizeof(r.error)-1);
        r.error[sizeof(r.error)-1] = '\0';
        break;
    }
    return r;
}
