/*
 * imu_motion.h — BMI270 6-axis IMU driver + gesture detection for Stick S3.
 * Emits EH_EVT_TAP / SHAKE / FLIP / STILL to the gateway (spec §7.3).
 */
#ifndef IMU_MOTION_H
#define IMU_MOTION_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_hermes.h"

#define EH_IMU_I2C_PORT    I2C_NUM_0
#define EH_IMU_SAMPLE_HZ   50
#define EH_SHAKE_THRESH    2.5f
#define EH_FLIP_THRESH     0.7f
#define EH_TAP_MAX_G       1.8f
#define EH_STILL_TIMEOUT_MS 3000

void imu_motion_init(void);
void imu_motion_task(void *pvParameters);
bool imu_read_accel(float *ax, float *ay, float *az);
bool imu_read_gyro(float *gx, float *gy, float *gz);

#endif /* IMU_MOTION_H */
