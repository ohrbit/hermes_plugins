/*
 * capabilities.c — board IO surface + handshake JSON. DRAFT (untested).
 *
 * NOTE (spec §8): the allowlist is split between firmware + gateway. The
 * firmware reports EVERYTHING it physically has here; the gateway config
 * (config/esp_hermes.yaml) holds the allowed_pins/allowed_i2c/blocked_pins
 * safety net and is authoritative. The firmware additionally hard-blocks
 * strapping pins via eh_pin_is_blocked() so a misconfigured gateway can never
 * drive boot/flash pins.
 */
#include "capabilities.h"
#include <string.h>
#include "esp_hermes.h"
#include "cJSON.h"

/* Board presets. Extend per hardware arrival (Stick-S3 vs Core-S3). */
static const int STICK_S3_PINS[] = {12, 13, 14, 36, 37, 38, 39};
static const uint8_t STICK_S3_I2C[] = {0x68}; /* IMU at 0x68 on Stick-S3 */

void eh_capabilities_init(eh_capabilities_t *caps, const char *board) {
    if (!caps) return;
    memset(caps, 0, sizeof(*caps));
    caps->imu = true;
    caps->speaker = true;
    caps->mic = true;
    caps->lcd = true;

    const int *pins = STICK_S3_PINS;
    int n = sizeof(STICK_S3_PINS) / sizeof(STICK_S3_PINS[0]);
    strncpy(caps->board, board ? board : "stick-s3", sizeof(caps->board) - 1);

    caps->pin_count = 0;
    for (int i = 0; i < n && caps->pin_count < EH_MAX_PINS; i++) {
        if (!eh_pin_is_blocked(pins[i])) {
            caps->pins[caps->pin_count++] = pins[i];
        }
    }
    caps->i2c_count = 0;
    for (size_t i = 0; i < sizeof(STICK_S3_I2C) / sizeof(STICK_S3_I2C[0]); i++) {
        if (caps->i2c_count < EH_MAX_I2C)
            caps->i2c_addr[caps->i2c_count++] = STICK_S3_I2C[i];
    }
}

char *eh_capabilities_to_json(const eh_capabilities_t *caps) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "capabilities");

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "board", caps->board);
    cJSON_AddBoolToObject(payload, "imu", caps->imu);
    cJSON_AddBoolToObject(payload, "speaker", caps->speaker);
    cJSON_AddBoolToObject(payload, "mic", caps->mic);
    cJSON_AddBoolToObject(payload, "lcd", caps->lcd);

    cJSON *pins = cJSON_CreateIntArray(caps->pins, caps->pin_count);
    cJSON_AddItemToObject(payload, "pins", pins);
    cJSON *i2c = cJSON_CreateArray();
    for (int i = 0; i < caps->i2c_count; i++)
        cJSON_AddItemToArray(i2c, cJSON_CreateNumber(caps->i2c_addr[i]));
    cJSON_AddItemToObject(payload, "i2c", i2c);

    cJSON_AddItemToObject(root, "payload", payload);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}
