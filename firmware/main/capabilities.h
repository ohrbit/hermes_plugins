/*
 * capabilities.h — report the device's IO surface to the gateway on connect
 * (spec §3.1, §5). The gateway uses this to know which pins/peripherals exist
 * so it can validate tool_calls against the allowlist (spec §8).
 */
#ifndef EH_CAPABILITIES_H
#define EH_CAPABILITIES_H

#include <stdbool.h>
#include "esp_hermes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EH_MAX_PINS   16
#define EH_MAX_I2C    8

typedef struct {
    int   pins[EH_MAX_PINS];     /* GPIO numbers usable (post-allowlist) */
    int   pin_count;
    uint8_t i2c_addr[EH_MAX_I2C];/* discovered I2C device addresses */
    int   i2c_count;
    bool  imu;                   /* 6-axis IMU present */
    bool  speaker;               /* I2S speaker present */
    bool  mic;                   /* I2S mic present */
    bool  lcd;                   /* display present */
    char  board[24];             /* e.g. "stick-s3" */
} eh_capabilities_t;

/* Build the capabilities from board defaults + NVS allowlist. */
void eh_capabilities_init(eh_capabilities_t *caps, const char *board);

/* Serialize to the WS "capabilities" handshake message (caller frees). */
char *eh_capabilities_to_json(const eh_capabilities_t *caps);

#ifdef __cplusplus
}
#endif
#endif /* EH_CAPABILITIES_H */
