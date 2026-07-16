/*
 * io_tools.h — IO tool handlers for gateway tool-calls (spec §3.4, §5).
 * gpio/i2c/pwm/uart/imu. All gated by eh_pin_is_blocked() + allowlist.
 */
#ifndef IO_TOOLS_H
#define IO_TOOLS_H

#include "esp_hermes.h"

void io_tools_init(void);

/* Execute a tool-call received from gateway. Returns result to be sent back. */
eh_tool_result_t io_tool_exec(eh_tool_t tool, const cJSON *params);

#endif /* IO_TOOLS_H */
