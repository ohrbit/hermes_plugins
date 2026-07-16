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
[ESP-Claw](https://github.com/m5stack/ESP-Claw)'s hardware layer
(`components/common`, `edge_agent` IMU) but strip its LLM client — Hermes is
the brain.

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

## Installation

Two parts: the **Hermes gateway plugin** (Python, install on your server) and
the **ESP32 firmware** (C/ESP-IDF, flash to the device). Do the gateway side
first — it works without hardware (tests run headless).

### Prerequisites

| Component | Version / Link | Notes |
|---|---|---|
| Hermes Agent | [docs](https://hermes-agent.nousresearch.com/docs) | gateway must be running (`hermes gateway status`) |
| Python | 3.11+ | gateway runs on this |
| ESP-IDF | v5.2+ — [official install guide](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32s3/get-started/index.html) | only for firmware; not needed for gateway |
| Git | any | to clone the plugin repo |
| M5Stack ESP32-S3 | [product page](https://m5stack.com/products/esp32-s3-stick) | hardware; arrives separately |

### Part 1 — Gateway Plugin (Hermes side)

```bash
# 1. Clone the plugin collection and enter the subdir
git clone https://github.com/ohrbit/hermes_plugins
cd hermes_plugins/esp-hermes

# 2. Install Python deps for the WS hub
pip install -r gateway/requirements.txt
#    (pulls: websockets, pydantic — see gateway/requirements.txt)

# 3. Register the plugin with Hermes
#    Copy the plugin folder into your Hermes plugins dir, then enable it:
hermes plugins install ./esp-hermes        # or symlink into ~/.hermes/plugins/
hermes config set gateway.platforms.esp_hermes.enabled true

# 4. Merge the device config (edit device_id + token to match your ESP)
cp config/esp_hermes.yaml ~/.hermes/config.yaml   # or merge by hand
#    See "Configuration" below for the full schema.

# 5. Restart the gateway so the adapter loads
hermes gateway restart

# 6. Verify the adapter imported cleanly (no tracebacks)
hermes gateway status
hermes plugins list | grep esp_hermes
```

> The gateway plugin is fully testable **without hardware**:
> `pytest tests/test_esp_hermes_adapter.py` spins up a real WS hub and asserts
> the adapter routes audio/events into a session. See
> [gateway/README.md](gateway/README.md) for the test matrix.

### Part 2 — ESP32 Firmware (device side)  🔄 DRAFT

> ⚠️ **Status:** the firmware is drafted but **not yet built or flashed** — no
> ESP32-S3 was available during development. The steps below are the intended
> flow; treat them as pending verification. Full detail in
> [firmware/README.md](firmware/README.md).

```bash
# 1. Install ESP-IDF (see link above), then export it:
. $HOME/esp/esp-idf/export.sh

# 2. Build the firmware
cd esp-hermes/firmware
idf.py set-target esp32s3
idf.py build

# 3. Flash + monitor (device on /dev/ttyUSB0)
idf.py flash monitor

# 4. On first boot, the device opens an AP / expects WiFi creds via NVS.
#    Set them once:  (exact mechanism TBD in firmware/README.md)
#    hermes config set esp_hermes.devices.stick-s3.wifi "SSID:password"
```

References for the firmware base:
- [ESP-Claw](https://github.com/m5stack/ESP-Claw) — hardware layer we fork
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32s3/) — build/flash

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
