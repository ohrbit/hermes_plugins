/*
 * io_tools.h — ESP-side IO tool handlers (spec §3.4, §5, §8).
 *
 * Executes gateway tool_calls on real hardware: gpio/i2c/pwm/uart/adc, plus a
 * delegate to the IMU for esp_imu_read, and a Phase-5 stub for esp_motor_set.
 *
 * SAFETY (spec §8): strapping/boot/flash pins are ALWAYS refused via
 * eh_pin_is_blocked() regardless of any allowlist. The gateway config
 * (config/esp_hermes.yaml allowed_pins) is the primary gate; this module adds
 * defense-in-depth so a misconfigured gateway can never drive dangerous pins.
 */
#ifndef EH_IO_TOOLS_H
#define EH_IO_TOOLS_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_hermes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Set the runtime allowed-pin list (from capabilities/NVS). Pins not hard
 * blocked but absent here are refused too (least-privilege). */
void eh_io_set_allowed_pins(const int *pins, int n);

/* Execute a decoded tool_call. Returns result; caller serializes tool_result. */
eh_tool_result_t eh_io_exec(eh_tool_t tool, const cJSON *params);

/* Parse a WS "tool_call" message -> exec -> build "tool_result" JSON string.
 * Caller frees the returned string. Returns NULL on parse failure. */
char *eh_io_handle_call(const cJSON *msg);

#ifdef __cplusplus
}
#endif
#endif /* EH_IO_TOOLS_H */
