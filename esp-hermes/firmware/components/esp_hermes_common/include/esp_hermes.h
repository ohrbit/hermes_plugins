/*
 * esp_hermes.h — shared types, protocol constants, and pin-safety policy for
 * the ESP-Hermes firmware (C/ESP-IDF).
 *
 * This header is the single source of truth for the wire protocol between the
 * ESP client and the Hermes gateway WS hub (gateway/esp_hermes_ws.py).
 * The message shapes below mirror implementation-spec.md §3 and the Python hub
 * verbatim. Keep them in sync — the gateway is the contract.
 *
 * Protocol summary (WebSocket, WSS recommended — spec §17):
 *   connect  wss://<gateway>/api/esp_hermes/ws?device_id=<id>&token=<key>
 *   first TX { "type":"capabilities", "payload":{...} }   -> RX { "type":"ack" }
 *
 *   ESP -> Gateway : audio | event | imu | tool_result | ping
 *   Gateway -> ESP : pet_state | video | audio | tool_call | mode_ack | pong | tui_line
 *   Binary WS frames carry raw audio (opus/pcm) — no base64 (hub handles both).
 */

#ifndef ESP_HERMES_H
#define ESP_HERMES_H

#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_websocket_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Build-time constants (mirror gateway tunables in esp_hermes_ws.py §12)
 * ------------------------------------------------------------------------- */
#define EH_WS_PATH          "/api/esp_hermes/ws"
#define EH_HANDSHAKE_TIMEOUT_MS 10000   /* must send capabilities within this */
#define EH_HEARTBEAT_TX_MS  30000        /* ping every 30s */
#define EH_HEARTBEAT_RX_MS  90000        /* drop if no pong for 90s */
#define EH_RECONNECT_MIN_MS 1000
#define EH_RECONNECT_MAX_MS 30000        /* exponential backoff cap */
#define EH_PING_FRAME_MS    25000        /* keep NAT alive (hub pings too) */

#define EH_DEVICE_ID_MAX    32
#define EH_TOKEN_MAX        64
#define EH_URL_MAX          256
#define EH_AUDIO_CHUNK_MS   200          /* capture/stream granularity */

/* ---------------------------------------------------------------------------
 * Message type enums
 * ------------------------------------------------------------------------- */
typedef enum {
    EH_MSG_AUDIO = 0,        /* ESP<->GW audio (uplink capture / TTS downlink) */
    EH_MSG_EVENT,            /* ESP->GW imu/button event (tap/shake/flip/still)*/
    EH_MSG_IMU,              /* ESP->GW accel+gyro vector */
    EH_MSG_TOOL_RESULT,      /* ESP->GW result of a tool_call */
    EH_MSG_PING,             /* ESP->GW liveness */
    EH_MSG_CAPABILITIES,     /* ESP->GW first message (pins/peripherals) */
    EH_MSG_ACK,              /* GW->ESP capabilities accepted */
    EH_MSG_PET_STATE,        /* GW->ESP pet state push */
    EH_MSG_VIDEO,            /* GW->ESP optional video burst (gif/mjpeg) */
    EH_MSG_TOOL_CALL,        /* GW->ESP IO tool invocation */
    EH_MSG_MODE_ACK,         /* GW->ESP mode accepted */
    EH_MSG_PONG,             /* GW->ESP liveness reply */
    EH_MSG_TUI_LINE,         /* GW->ESP terminal line for TUI scrollback */
    EH_MSG_UNKNOWN
} eh_msg_type_t;

/* Audio codec (spec §11 open question — default PCM until Opus wired). */
typedef enum {
    EH_AUDIO_PCM = 0,
    EH_AUDIO_OPUS,
    EH_AUDIO_BINARY   /* raw binary WS frame, codec implied by negotiated cap */
} eh_audio_format_t;

/* Pet / agent lifecycle states (spec §6). Plus IMU-coupled custom states (§6). */
typedef enum {
    EH_PET_IDLE = 0,
    EH_PET_RUN,
    EH_PET_REVIEW,
    EH_PET_ERROR,
    EH_PET_DONE,
    EH_PET_TILT,
    EH_PET_SHAKE,
    EH_PET_STRETCH
} eh_pet_state_t;

/* Physical events emitted by IMU/button (spec §7.3). */
typedef enum {
    EH_EVT_TAP = 0,
    EH_EVT_SHAKE,
    EH_EVT_FLIP,
    EH_EVT_STILL
} eh_event_t;

/* Input modes. */
typedef enum {
    EH_MODE_PTT = 0,   /* push-to-talk: hold button -> record -> release */
    EH_MODE_VAD        /* always-on: on-device voice activity detection */
} eh_mode_t;

/* IO tool identifiers (spec §3.4). */
typedef enum {
    EH_TOOL_GPIO_SET = 0,
    EH_TOOL_GPIO_READ,
    EH_TOOL_ADC_READ,
    EH_TOOL_PWM_SET,
    EH_TOOL_I2C_READ,
    EH_TOOL_I2C_WRITE,
    EH_TOOL_UART_SEND,
    EH_TOOL_IMU_READ,
    EH_TOOL_MOTOR_SET,
    EH_TOOL_COUNT
} eh_tool_t;

/* ---------------------------------------------------------------------------
 * Pin safety policy (spec §8). Hard-blocked pins are NEVER exposed regardless
 * of the allowlist. Allowed pins come from NVS/config at runtime.
 * ------------------------------------------------------------------------- */
/* Boot/flash/UART0 strapping pins — must never be controllable from gateway. */
#define EH_BLOCKED_PINS_DEFAULT { 0, 1, 2, 3, 45, 46 }  /* GPIO0/1/2/3 + straps */
#define EH_BLOCKED_PINS_COUNT   6

/* Board pin map (M5Stack Stick S3, SKU K150 — verified m5-docs). */
#define EH_PIN_IMU_SDA   47
#define EH_PIN_IMU_SCL   48
#define EH_PIN_KEY1      37
#define EH_PIN_KEY2      38
#define EH_PIN_MIC_WS    8
#define EH_PIN_MIC_SCK   9
#define EH_PIN_MIC_SD    10
#define EH_PIN_SPK_WS    15
#define EH_PIN_SPK_SCK   16
#define EH_PIN_SPK_SD    17

/* LCD (ST7789, 240x240, SPI) — M5Stack Stick S3 SKU K150. */
#define EH_PIN_LCD_MOSI  11
#define EH_PIN_LCD_SCLK  13
#define EH_PIN_LCD_CS    10
#define EH_PIN_LCD_DC    12
#define EH_PIN_LCD_RST   14
#define EH_LCD_W         240
#define EH_LCD_H         240

/* Send a raw event to the gateway over the open WS handle. */
void eh_send_event(esp_websocket_client_handle_t ws, eh_event_t evt);

/* Result of an IO tool execution on-device. */
typedef struct {
    bool      ok;
    int32_t   value;       /* numeric result (level/mV/bytes) or 0 */
    char      value_str[64]; /* textual result (e.g. "ax,ay,az") when value<0 */
    char      error[48];   /* human readable on failure */
} eh_tool_result_t;

/* ---------------------------------------------------------------------------
 * cJSON read helpers (defensive — gateway JSON must not crash the device)
 * ------------------------------------------------------------------------- */
static inline const char *eh_json_str(const cJSON *obj, const char *key) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (it && cJSON_IsString(it)) ? it->valuestring : NULL;
}
static inline int eh_json_int(const cJSON *obj, const char *key, int def) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (it && cJSON_IsNumber(it)) ? it->valueint : def;
}
static inline bool eh_json_bool(const cJSON *obj, const char *key, bool def) {
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (it && cJSON_IsBool(it)) ? cJSON_IsTrue(it) : def;
}

/* ---------------------------------------------------------------------------
 * String<->enum mapping helpers (shared by parser + serializer)
 * ------------------------------------------------------------------------- */
const char *eh_pet_state_str(eh_pet_state_t s);
eh_pet_state_t eh_pet_state_from_str(const char *s);
const char *eh_event_str(eh_event_t e);
eh_event_t eh_event_from_str(const char *s);
const char *eh_tool_str(eh_tool_t t);
eh_tool_t eh_tool_from_str(const char *s);
const char *eh_mode_str(eh_mode_t m);
eh_mode_t eh_mode_from_str(const char *s);
const char *eh_audio_fmt_str(eh_audio_format_t f);

/* Build the WS URL including device_id + token query params. Returns length. */
int eh_build_ws_url(char *buf, size_t buflen,
                    const char *gateway_host,   /* e.g. "gw.example.com" */
                    const char *device_id,
                    const char *token);

/* True if pin is in the hard-blocked list (never exposed). */
bool eh_pin_is_blocked(int pin);

#ifdef __cplusplus
}
#endif
#endif /* ESP_HERMES_H */
