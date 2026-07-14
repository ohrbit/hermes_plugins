/*
 * imu_motion.h — 6-axis IMU read + gesture/event detection (spec §7.3, §6).
 *
 * Provides:
 *   - eh_imu_read(): raw accel+gyro vector (polled; used by esp_imu_read tool)
 *   - eh_imu_poll_events(): event pump -> emits EH_EVT_* with on-device
 *     debounce (low-pass + threshold + N stable samples) so the gateway is not
 *     spammed (skill safety rule #5).
 *   - eh_imu_pose(): smoothed orientation -> pose_x/pose_y for pet lean (§6).
 */
#ifndef EH_IMU_MOTION_H
#define EH_IMU_MOTION_H

#include <stdbool.h>
#include "esp_hermes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float ax, ay, az;   /* accel m/s^2 */
    float gx, gy, gz;   /* gyro rad/s */
} eh_imu_vec_t;

/* Initialize IMU (board defaults; Stick-S3 IMU on I2C 0x68). */
esp_err_t eh_imu_init(void);

/* Raw read (polled). */
esp_err_t eh_imu_read(eh_imu_vec_t *out);

/*
 * Event pump — call periodically from the main loop. Returns the latest event
 * (or EH_EVT_STILL if none newly detected). Debounced on-device.
 * Maps (spec §8 gestures):
 *   shake -> toggle_mode, tap -> ptt_trigger, flip -> wake
 * (gesture->action mapping is owned by the gateway config; the firmware only
 *  emits the raw event name, keeping the device a thin client.)
 */
eh_event_t eh_imu_poll_events(void);

/* Smoothed pose offset for pet lean (px, range ~[-20,20]). */
void eh_imu_pose(int *pose_x, int *pose_y);

#ifdef __cplusplus
}
#endif
#endif /* EH_IMU_MOTION_H */
