/*
 * io_tools.h — public API for the ESP-Hermes IO tool channel.
 * Declares the on-device executor used by the gateway tool_call handler.
 * Pin safety: every physical access is gated by eh_pin_is_blocked() +
 * a runtime allowlist (see io_tools.c). Never expose strapping pins.
 */
#ifndef IO_TOOLS_H
#define IO_TOOLS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_hermes.h"
#include "imu_motion.h"
#include "cJSON.h"

/*
 * Allowlist of pins the gateway IS permitted to drive (spec §8).
 * Strapping/boot/flash pins are hard-blocked in esp_hermes.c regardless
 * of this list. Deny-by-default: a pin not in this list is rejected.
 * Stick S3 exposed GPIOs that are safe to expose as IO tools:
 *   G11 (KEY1/PTT), G12 (KEY2/mode), G39..G46 (LCD/IMU side), plus a few
 *   user-expandable pins. Adjust per device via NVS if needed.
 */
#define EH_ALLOWED_PINS_DEFAULT { 11, 12, 13, 14, 15, 16, 17, 18, 38, 39, 40, 41, 45, 46, 47, 48 }
#define EH_ALLOWED_PINS_COUNT   16

/* Initialise the IO tool subsystem (install LEDC fade, load allowlist). */
void io_tools_init(void);

/*
 * Execute a single IO tool call.
 * Returns a result struct; r.ok is false on failure (r.error set).
 * Caller owns r.value_s (malloc'd) and must free it.
 */
eh_tool_result_t io_tool_exec(eh_tool_t tool, const cJSON *params);

#endif /* IO_TOOLS_H */
