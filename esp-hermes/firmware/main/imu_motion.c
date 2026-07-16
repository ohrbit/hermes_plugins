/*
 * imu_motion.c — BMI270 driver (I2C 0x68) + on-device gesture detection.
 * Stick S3: SCL=G48, SDA=G47. ES8311 + M5PM1 share this bus.
 */
#include "imu_motion.h"
#include "driver/i2c.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "math.h"

static const char *TAG = "imu_motion";
static esp_websocket_client_handle_t s_ws = NULL;

#define BMI270_CHIP_ID   0x24
#define REG_CHIP_ID     0x00
#define REG_ACC_DATA    0x0C
#define REG_GYR_DATA    0x12
#define REG_ACC_CONF    0x40
#define REG_PWR_CTRL    0x7D
#define REG_CMD         0x7E

static bool i2c_reg_write(uint8_t reg, uint8_t v) {
    uint8_t buf[2] = {reg, v};
    return i2c_master_write_to_device(EH_IMU_I2C_PORT, EH_I2C_IMU_ADDR,
            buf, 2, pdMS_TO_TICKS(50)) == ESP_OK;
}
static bool i2c_reg_read(uint8_t reg, uint8_t *v) {
    return i2c_master_write_read_device(EH_IMU_I2C_PORT, EH_I2C_IMU_ADDR,
            &reg, 1, v, 1, pdMS_TO_TICKS(50)) == ESP_OK;
}
static int16_t read_axis(uint8_t base) {
    uint8_t lo, hi;
    i2c_reg_read(base, &lo); i2c_reg_read(base + 1, &hi);
    return (int16_t)((hi << 8) | lo);
}

void imu_motion_init(void) {
    i2c_config_t c = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = EH_PIN_IMU_SDA,
        .scl_io_num = EH_PIN_IMU_SCL,
        .master.clk_speed = 400000,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
    };
    i2c_param_config(EH_IMU_I2C_PORT, &c);
    i2c_driver_install(EH_IMU_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);

    uint8_t id = 0;
    if (i2c_reg_read(REG_CHIP_ID, &id) && id == BMI270_CHIP_ID)
        ESP_LOGI(TAG, "BMI270 found (id=0x%02x)", id);
    else
        ESP_LOGW(TAG, "BMI270 not found (got 0x%02x)", id);
    i2c_reg_write(REG_PWR_CTRL, 0x0E);
    i2c_reg_write(REG_CMD, 0x02);
}

bool imu_read_accel(float *ax, float *ay, float *az) {
    *ax = read_axis(REG_ACC_DATA) / 4096.0f;
    *ay = read_axis(REG_ACC_DATA + 2) / 4096.0f;
    *az = read_axis(REG_ACC_DATA + 4) / 4096.0f;
    return true;
}
bool imu_read_gyro(float *gx, float *gy, float *gz) {
    *gx = read_axis(REG_GYR_DATA) / 16.4f;
    *gy = read_axis(REG_GYR_DATA + 2) / 16.4f;
    *gz = read_axis(REG_GYR_DATA + 4) / 16.4f;
    return true;
}

void imu_motion_task(void *pvParameters) {
    s_ws = (esp_websocket_client_handle_t)pvParameters;
    float ax, ay, az;
    int64_t last_motion = esp_timer_get_time();
    bool was_still = true;
    const TickType_t period = pdMS_TO_TICKS(1000 / EH_IMU_SAMPLE_HZ);

    for (;;) {
        imu_read_accel(&ax, &ay, &az);
        float mag = sqrtf(ax*ax + ay*ay + az*az);
        int64_t now = esp_timer_get_time();

        if (mag > EH_SHAKE_THRESH) {
            eh_send_event(s_ws, EH_EVT_SHAKE);
            last_motion = now; was_still = false;
        } else if (mag > EH_TAP_MAX_G && mag < EH_SHAKE_THRESH) {
            eh_send_event(s_ws, EH_EVT_TAP);
            last_motion = now; was_still = false;
        }
        if (az < -EH_FLIP_THRESH) {
            eh_send_event(s_ws, EH_EVT_FLIP);
            last_motion = now; was_still = false;
        }
        if (mag < 1.2f && (now - last_motion) > EH_STILL_TIMEOUT_MS * 1000) {
            if (!was_still) { eh_send_event(s_ws, EH_EVT_STILL); was_still = true; }
        }
        vTaskDelay(period);
    }
}
