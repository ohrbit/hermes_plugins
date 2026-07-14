# ESP-Hermes Channel

> Turn an M5Stack ESP32-S3 into a first-class Hermes voice/IO channel — Hermes (gateway) is the brain, the ESP is a thin physical client. Parity with Telegram/Desktop, plus a body.

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Hermes](https://img.shields.io/badge/powered%20by-Hermes%20Agent-orange.svg)](https://hermes-agent.nousresearch.com)

## Why ESP-Hermes?

Edge AI boxes (DGX, Jetson) are overkill for a personal voice companion. The
ESP32-S3 costs a few dollars, fits in your palm, and — wired to Hermes as a
**channel** (not a standalone agent) — becomes a physical extension of your
agent: talk to it, it talks back, it blinks your LEDs, it reacts to being
shaken. Your agent gets a body.

## Features

- ✅ **Voice channel** — push-to-talk + always-on (VAD), STT→Hermes→TTS
- ✅ **petdex pet on LCD** — agent states reflected as a live companion
- ✅ **Full hardware access** — GPIO / I2C / SPI / ADC / PWM / UART via tool-calls
- ✅ **IMU-driven** — accelerometer + gyroscope: shake/tap/flip trigger actions
- ✅ **Embodied** — movement in 3D space is input; optional motor control later
- ✅ **TUI mode** — pocket-sized terminal scrollback on the LCD
- ✅ **Slash commands** — `/mode`, `/pet`, `/gpio`, `/notify`, … like Telegram
- 🔄 **Video bursts** — short GIF/MJPEG on state change (planned)
- 🔄 **Motor control** — Hermes nods/shakes in physical space (Phase 5)

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  M5Stack ESP32-S3  (esp-hermes firmware, C/ESP-IDF)           │
│  mic ─▶ audio ─WS▶ │ button/IMU ─WS▶ │ LCD ◀─pet_state/video  │
│  speaker ◀─TTS ─WS─ │ GPIO/I2C ─WS─                                   │
└─────────────────────────────────────────────────────────────┘
                              │ WebSocket (WSS)
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  Hermes Gateway (:9119)                                        │
│   esp_hermes adapter · WS hub · STT · Agent · TTS · petdex     │
└─────────────────────────────────────────────────────────────┘
```

**Key principle:** ESP is a *gateway client*, not an LLM client. We reuse
ESP-Claw's hardware layer (`components/common`, `edge_agent` IMU) but strip its
LLM client — Hermes is the brain.

## Repository Layout

```
esp-hermes/
├── plugin.yaml       # Hermes plugin manifest (kind: platform)
├── __init__.py       # plugin entry point → register()
├── gateway/          # Hermes-side adapter (Python)
│   ├── esp_hermes.py     # ✅ EspHermesAdapter (BasePlatformAdapter subclass)
│   └── esp_hermes_ws.py  # ✅ WS hub + pet-state push (delivered, self-contained)
├── tools/            # IO-tool handlers (Python)
│   └── esp_io.py     # esp_gpio_set, esp_i2c_read, esp_imu_read, ...
├── firmware/         # ESP32-S3 client (C/ESP-IDF) — DRAFT until hardware
│   └── README.md     # fork plan, build steps, file map
├── config/           # config.yaml snippets (allowlist, gestures)
│   └── esp_hermes.yaml   # sample (no secrets)
├── tests/            # adapter unit tests (real WSHub, no hardware)
└── references/       # Full implementation spec (protocol, phases)
    └── implementation-spec.md
```

## Quick Start (when hardware arrives)

```bash
# 1. Clone
git clone https://github.com/ohrbit/hermes_plugins
cd hermes_plugins/esp-hermes

# 2. Build firmware (requires ESP-IDF)
idf.py set-target esp32s3
idf.py build && idf.py flash

# 3. Enable gateway adapter (Hermes side)
hermes config set gateway.platforms.esp_hermes.enabled true

# 4. Point ESP at your gateway WebSocket URL + device token
```

## Configuration

```yaml
# config/esp_hermes.yaml (merged into ~/.hermes/config.yaml)
esp_hermes:
  devices:
    stick-s3:
      allowed_pins: [12, 13, 14, 36]
      allowed_i2c: [0x40, 0x68]
      blocked_pins: [0, 1, 2, 3]        # boot/flash never exposed
      auto_approve_safe: false
      rate_limit: { gpio_set: 5/s, pwm_set: 5/s }
  gestures:
    shake: toggle_mode
    tap: ptt_trigger
    flip: wake
  easter_eggs:
    secret_shake: pet_dance
    upside_down_3s: pet_sleep
```

## Commands (on-device, like Telegram)

| Cmd | Action |
|---|---|
| `/mode ptt\|vad` | switch input mode |
| `/pet <slug>` | change pet (uses `hermes pets`) |
| `/display pet\|tui` | toggle LCD mode |
| `/status` | battery, wifi, session |
| `/gpio <pin> <H\|L>` | direct IO (allowlist + approval) |
| `/notify <msg>` | push to Telegram |
| `/help` | list commands |

## Development

Tracked via Kanban board `esp-hermes` (Hermes). Cards map to spec sections.
JIT agent teams build gateway/tools/config on Modal; firmware is drafted until
hardware arrives.

See [`references/implementation-spec.md`](references/implementation-spec.md)
for the full protocol, phases, and safety design.

## Safety

Permanent hardware access (relays, motors) demands guardrails: pin allowlist,
destructive-action approval, rate-limiting, power-pin hard-block, TLS, replay
protection, and audit logging. See spec §8, §17.

## License

MIT — © 2026 ohrbit
