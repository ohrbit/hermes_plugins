# ESP-Hermes Gateway Adapter

Two pieces live here (implementation-spec §4):

| File | Role | Status |
|------|------|--------|
| `esp_hermes_ws.py` | **WS hub + pet-state push.** Bidirectional WebSocket server: device connect/auth, inbound audio/event/imu routing, outbound pet_state/TTS/tool_call, IO tool round-trip, heartbeat, audit log. Self-contained + unit-testable. | ✅ delivered |
| `esp_hermes.py` | **`BasePlatformAdapter` subclass** (`EspHermesAdapter`). Registers the channel with Hermes via the plugin seam (`register()` → `ctx.register_platform`), feeds inbound audio/events/imu into the conversation like a Telegram message, pushes replies as TTS audio + pet_state, drives lifecycle pet states, and supports proactive pushes (cron/agent) + IO tool round-trips. | ✅ delivered |

`esp_hermes.py` ships as a **Hermes plugin** (`kind: platform`): drop the repo at
`~/.hermes/plugins/esp-hermes/` (or install `ohrbit/hermes_plugins` → `esp-hermes/`)
and enable it:

```bash
hermes config set gateway.platforms.esp_hermes.enabled true
```

## How it fits together

```
ESP32-S3 ──WS/WSS──▶ WSHub (esp_hermes_ws.py)
                       │  inbound handler
                       ▼
                 EspHermesAdapter._on_inbound
                       │  MessageEvent
                       ▼
              BasePlatformAdapter.handle_message  ──▶ agent session (per device_id)
                       ▲
              send() / send_voice() / send_pet_state() / push_proactive()
                       │
                       ▼
                 WSHub.push / call_tool  ──▶ ESP32-S3 LCD + speaker + GPIO
```

- **Inbound:** `_on_inbound(device_id, msg)` builds a `MessageEvent`
  (`build_source(chat_id=device_id)`) and calls the inherited `handle_message`,
  so the device inherits the full gateway pipeline (auth, slash commands,
  sessions, interrupts, tools, memory) for free.
- **Outbound:** `send` renders TTS audio (voice downlink); `send_voice` streams
  a file; `send_pet_state` pushes the agent-lifecycle pet to the LCD.
- **Lifecycle:** `on_processing_start`/`on_processing_complete` push `run`/`done`/
  `error` pet states. `push_proactive` delivers cron/alert pushes (spec §13).
- **IO tools:** `call_io_tool` → `WSHub.call_tool` round-trips to the device;
  `tools/esp_io.py` is the dispatcher (safety/allowlist enforced upstream).

## Tests

`tests/test_esp_hermes_adapter.py` exercises routing, sends, pet-state, lifecycle
hooks, proactive push, and IO round-trip against the real `WSHub` (no hardware):

```bash
PYTHONPATH=/usr/local/lib/hermes-agent python3 tests/test_esp_hermes_adapter.py
```
