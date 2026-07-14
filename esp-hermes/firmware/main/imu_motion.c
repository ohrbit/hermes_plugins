/*
 * imu_motion.c — IMU read + debounced event detection. DRAFT (no HW).
 *
 * Detection is deliberately simple + debounced (spec §8 safety #5): a gesture
 * is only emitted after N consecutive stable samples cross a threshold, so a
 * single noisy spike never reaches the gateway. Tuning (thresholds, N) is set
 * at hardware arrival.
 */
#include "imu_motion.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static const char *TAG = "eh_imu";
static bool s_init = false;

/* thresholds (tune on HW) */
#define IMU_SHAKE_G_THRESH  12.0f   /* m/s^2 above gravity => shake */
#define IMU_FLIP_DOT_THRESH 0.3f    /* orientation flip */
#define IMU_TAP_ACC_THRESH  18.0f   /* sharp accel spike => tap */
#define IMU_STABLE_SAMPLES  3       /* consecutive stable hits before emit */
#define IMU_STILL_MS        3000    /* quiet window => 'still' */

static eh_imu_vec_t s_last;
static int s_shake_cnt = 0, s_tap_cnt = 0, s_flip_cnt = 0;
static int64_t s_last_event_ms = 0;
static eh_event_t s_pending = EH_EVT_STILL;
static eh_imu_vec_t s_pose_filt;

esp_err_t eh_imu_init(void) {
    if (s_init) return ESP_OK;
    memset(&s_last, 0, sizeof(s_last));
    memset(&s_pose_filt, 0, sizeof(s_pose_filt));
    /* TODO(hw): init I2C + IMU driver (e.g. MPU6886 on Stick-S3 @0x68). */
    s_init = true;
    return ESP_OK;
}

esp_err_t eh_imu_read(eh_imu_vec_t *out) {
    if (!s_init || !out) return ESP_ERR_INVALID_STATE;
    /* TODO(hw): read IMU registers -> fill out. Placeholder returns last. */
    *out = s_last;
    return ESP_OK;
}

static float magnitude(const eh_imu_vec_t *v) {
    return sqrtf(v->ax * v->ax + v->ay * v->ay + v->az * v->az);
}

eh_event_t eh_imu_poll_events(void) {
    if (!s_init) return EH_EVT_STILL;
    eh_imu_vec_t v;
    if (eh_imu_read(&v) != ESP_OK) return EH_EVT_STILL;

    /* exponential smoothing for pose */
    const float a = 0.2f;
    s_pose_filt.ax += a * (v.ax - s_pose_filt.ax);
    s_pose_filt.ay += a * (v.ay - s_pose_filt.ay);
    s_pose_filt.az += a * (v.az - s_pose_filt.az);

    float mag = magnitude(&v);
    int64_t now = esp_timer_get_time() / 1000;

    /* shake: |a| well above 1g sustained */
    if (mag > IMU_SHAKE_G_THRESH) s_shake_cnt++; else s_shake_cnt = 0;
    /* tap: sharp high accel spike */
    if (mag > IMU_TAP_ACC_THRESH) s_tap_cnt++;   else s_tap_cnt = 0;
    /* flip: z-axis orientation crosses threshold (device turned over) */
    if (fabsf(v.az) < IMU_FLIP_DOT_THRESH) s_flip_cnt++; else s_flip_cnt = 0;

    if (s_shake_cnt >= IMU_STABLE_SAMPLES) { s_pending = EH_EVT_SHAKE; s_shake_cnt = 0; }
    else if (s_tap_cnt >= IMU_STABLE_SAMPLES) { s_pending = EH_EVT_TAP; s_tap_cnt = 0; }
    else if (s_flip_cnt >= IMU_STABLE_SAMPLES) { s_pending = EH_EVT_FLIP; s_flip_cnt = 0; }
    else if (now - s_last_event_ms > IMU_STILL_MS) s_pending = EH_EVT_STILL;

    if (s_pending != EH_EVT_STILL) {
        s_last_event_ms = now;
        eh_event_t e = s_pending;
        s_pending = EH_EVT_STILL;
        s_last = v;
        return e;
    }
    s_last = v;
    return EH_EVT_STILL;
}

void eh_imu_pose(int *pose_x, int *pose_y) {
    /* map accel (tilt) to a few px of lean */
    if (pose_x) *pose_x = (int)(s_pose_filt.ax * 2.0f);
    if (pose_y) *pose_y = (int)(s_pose_filt.ay * 2.0f);
}
