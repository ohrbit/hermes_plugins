# ESP-Hermes Channel — Full Implementation Spec

**Goal:** Turn an M5Stack ESP32-S3 into a first-class Hermes voice/IO channel
(parity with Telegram/Desktop), where Hermes (gateway) is the brain and the
ESP is a thin physical client. NOT a standalone ESP-Claw agent.

**Status:** Design frozen. Build when hardware arrives.

---

## 1. Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│  M5Stack ESP32-S3  (esp-hermes firmware, C/Arduino + ESP-IDF) │
│                                                               │
│  mic ──▶ audio encode ──WS──▶                                │
│  button/IMU ──▶ event ──────WS──▶  Hermes Gateway (:9119)    │
│  LCD ◀── pet_state / video ◀──WS──                          │
│  speaker ◀── TTS audio ◀────────WS──                         │
│  GPIO/I2C/PWM/UART ◀── tool-result ◀──WS──                    │
└─────────────────────────────────────────────────────────────┘
                              │
                    WebSocket (bidirectional)
                              │
┌─────────────────────────────▼─────────────────────────────┐
│  Hermes Gateway (hermes-agent, /usr/local/lib/hermes-agent) │
│   - esp_hermes platform adapter (NEW)                        │
│   - STT (local faster-whisper / groq)                        │
│   - Agent reasoning + tools + memory                         │
│   - TTS (Edge TTS)                                           │
│   - petdex renderer → pet_state push                         │
│   - IO-tool layer (esp_gpio_set, esp_imu_read, ...)          │
└─────────────────────────────────────────────────────────────┘
```

**Key principle:** ESP is a *gateway client*, not an LLM client. Do NOT use
ESP-Claw's LLM mode. The ESP never calls OpenAI/Anthropic directly.

---

## 2. Reuse Map (ESP-Claw vs. new)

### 2.1 Reuse from ESP-Claw (fork, don't rewrite)
Repo: `https://github.com/m5stack/ESP-Claw` (C/ESP-IDF)

| ESP-Claw component | Reuse as | Notes |
|---|---|---|
| `components/common` | WiFi manager, HTTP client, audio I/O, LCD drivers | Hardware abstraction — saves weeks |
| `components/lua_modules` | Lua loader (optional, for on-device config) | Not needed for v1 |
| `components/claw_modules` | Capability struct pattern | Adapt to `capabilities` msg |
| `application/edge_agent` | IMU/motion detection, event loop | **Strip the LLM client**; keep IMU + event pump |

**What to DELETE from ESP-Claw:**
- The LLM client (OpenAI/Anthropic HTTP calls)
- The on-device agent loop / tool-execution
- Lua self-programming (not wanted — Hermes is the brain)

### 2.2 New code
| Component | Where | Language |
|---|---|---|
| `esp_hermes` gateway adapter | `gateway/platforms/esp_hermes.py` | Python (Hermes) |
| WS bridge + pet-state hub | `gateway/platforms/esp_hermes_ws.py` | Python |
| IO-tool handlers | `tools/esp_io.py` | Python |
| ESP client firmware | `application/esp_hermes_client/` (fork of edge_agent) | C/ESP-IDF |
| Gesture→task config | `config.yaml` `esp_hermes.devices.<id>.gestures` | YAML |

### 2.3 Hermes source to study (local: `/usr/local/lib/hermes-agent`)
- `gateway/platforms/` — `BasePlatformAdapter` base class, `send()`, `send_voice()`,
  `connect()`, `disconnect()`, `_is_dm_allowed()`. Telegram/Discord are templates.
- `gateway/run.py` — how platforms register + route messages.
- `tools/registry.py` — `registry.register()` for IO tools.
- API server (`/v1/chat/completions`) — already OpenAI-compatible; ESP could
  use it OR the WS bridge (WS preferred for bidirectional push).

---

## 3. Protocol (WebSocket)

### 3.1 Connection
```
ESP ──▶ WS connect wss://<gateway>/api/esp_hermes/ws?device_id=<id>&token=<key>
Gateway ──▶ 101 Switching Protocols
ESP ──▶ { "type": "capabilities", "payload": { pins:[...], i2c:[...], imu:true } }
Gateway ──▶ { "type": "ack", "device_id": "<id>" }
```

### 3.2 Message types (ESP → Gateway)
```json
{ "type": "audio",    "format": "opus", "data": "<base64>", "mode": "ptt|vad" }
{ "type": "event",    "name": "tap|shake|flip|still", "ts": 1234567890 }
{ "type": "imu",      "accel": [x,y,z], "gyro": [x,y,z] }   // polled or streamed
{ "type": "tool_result", "call_id": "<id>", "ok": true, "value": 512 }
{ "type": "ping" }
```

### 3.3 Message types (Gateway → ESP)
```json
{ "type": "pet_state", "state": "idle|run|review|error|done|tilt|shake|stretch" }
{ "type": "video",     "clip_id": "done_celebrate", "format": "gif" }   // optional
{ "type": "audio",     "format": "opus|pcm", "data": "<base64>" }       // TTS downlink
{ "type": "tool_call", "call_id": "<id>", "tool": "esp_gpio_set",
  "params": { "pin": 12, "state": "HIGH" } }
{ "type": "mode_ack",  "mode": "ptt|vad" }
{ "type": "pong" }
```

### 3.4 IO-Tool schema (gateway → ESP → Arduino handler)
```
esp_gpio_set   { pin, state: "HIGH"|"LOW" }
esp_gpio_read  { pin }                       → returns level
esp_adc_read   { pin, atten }               → returns mV
esp_pwm_set    { pin, duty, freq }          → ledc
esp_i2c_read   { addr, reg, len }           → bytes
esp_i2c_write  { addr, reg, data: [] }
esp_uart_send  { data }                      → Serial passthrough
esp_imu_read   { axis: "all"|"accel"|"gyro" } → vector
esp_motor_set  { angle|speed }               // Phase 5 (motor attached)
```

---

## 4. Gateway Adapter (`gateway/platforms/esp_hermes.py`)

Skeleton (mirrors `BasePlatformAdapter`):

```python
class EspHermesAdapter(BasePlatformAdapter):
    platform_name = "esp_hermes"

    async def connect(self, *, is_reconnect=False) -> bool:
        # start WS server / accept connections from esp_hermes_ws
        ...

    async def send(self, chat_id, text, metadata=None):
        # chat_id == device_id; push text or TTS audio
        await self.ws_hub.push(chat_id, {"type": "audio", ...})

    async def send_voice(self, chat_id, audio_path, metadata=None):
        # stream TTS file to device
        ...

    async def send_pet_state(self, chat_id, state):
        await self.ws_hub.push(chat_id, {"type": "pet_state", "state": state})

    # Inbound audio/event → routed into Hermes conversation like Telegram msg
    async def _on_audio(self, device_id, audio_b64):
        text = stt(audio_b64)
        # inject as user message into device's session
```

Registration in `gateway/run.py`: add `esp_hermes` to platform registry +
config block `gateway.platforms.esp_hermes.enabled: true`.

---

## 5. Firmware (`application/esp_hermes_client/`)

Fork `application/edge_agent`, strip LLM client, add:

```
main/
  esp_hermes_client.c        // WS client, msg dispatch
  audio_capture.c/.h         // I2S mic → opus encode
  audio_play.c/.h            // opus decode → I2S speaker
  lcd_pet.c/.h               // petdex sprite + GIF render
  imu_motion.c/.h            // MPU/accel+gyro → events + pose
  io_tools.c/.h              // gpio/i2c/pwm/uart handlers
  capabilities.c/.h          // report pins/peripherals on connect
  nvs_config.c/.h            // persist mode, device_id, token
```

Build: `idf.py set-target esp32s3 && idf.py build && idf.py flash`

---

## 6. petdex on LCD

- petdex renderer runs on **gateway** (Hermes side). See `productivity/petdex` skill.
- Gateway pushes `pet_state` + frame data over WS.
- ESP LCD renders frames. States: `idle`, `run`, `review`, `error`, `done`.
- **Scope:** petdex = agent-lifecycle states (out-of-the-box). Physical IMU
  coupling (lean/dizzy) = custom layer:
  - Path A (v1): map IMU → existing states (`run`=busy, `done`=content)
  - Path B (later): custom pet states `tilt`/`shake`/`stretch` (needs frames)
- **Video on state change (optional):** Gateway pushes `video: <clip>`;
  ESP plays MJPEG/GIF 1–3s burst (no H.264 — no HW decoder on S3).
  Best mix: sprite pet persistent, video only on event.

### 6.5 TUI Display on LCD (optional, terminal-style view)

Mirroring the desktop/Hermes TUI aesthetic on the ESP LCD — a compact
terminal view alongside (or instead of) the pet.

**Concept:**
- Split or toggle screen:
  - **Pet mode:** petdex sprite (idle/run/done) — default
  - **TUI mode:** scrollback terminal showing the live conversation
  - Toggle via gesture (e.g. double-tap) or gateway config push
- TUI shows: user prompt → Hermes response (truncated to fit), mode indicator,
  connection status, token usage (if pushed), pet_state as a side glyph.

**Rendering constraints (ESP32-S3 small panel, ~240×240):**
- Monospace 6×8 or 8×8 font, ~30×30 chars max. Keep scrollback buffer ~20 lines.
- Use a ring buffer; newest line at bottom, auto-scroll.
- Color: 16-bit RGB565; accent color for user vs. agent lines (like desktop TUI).
- Word-wrap long lines; truncate overflow with `…`.

**Gateway side:**
- Push `tui_line` messages: `{ "type": "tui_line", "role": "user|agent", "text": "..." }`
- Or push full `tui_state` snapshot on change (simpler, avoids stream sync).
- Pet-state still pushed concurrently (pet renders in a corner glyph even in TUI mode).

**Implementation (firmware):**
```
lcd_tui.c/.h   // ring buffer, render scrollback, status bar, mode toggle
```
Reuses `lcd_pet.c` framebuffer; both draw to same panel, switched by `display_mode`.

**Best mix:** Pet as ambient companion + TUI as info layer (corner or toggle).
User gets a pocket-sized Hermes terminal.

---

## 7. Modes

1. **Push-to-talk (PTT):** Hold button → record → release → upload. Pet `recording`.
2. **Always-on (VAD):** Continuous mic, on-device VAD triggers upload on speech.
   Pet `listening` → `run` while processing.
Mode toggle: button short-press OR shake gesture. Persist in NVS.

---

## 8. Safety (CRITICAL)

`config.yaml`:
```yaml
esp_hermes:
  devices:
    stick-s3:
      allowed_pins: [12, 13, 14, 36]
      allowed_i2c: [0x40, 0x68]
      blocked_pins: [0, 1, 2, 3]     # boot/flash/UART0 never exposed
      auto_approve_safe: false        # destructive IO prompts user
      rate_limit: { gpio_set: 5/s, pwm_set: 5/s }
  gestures:
    shake: toggle_mode
    tap: ptt_trigger
    flip: wake
    double_tap_tilt: task:summarize_day
    figure8: task:status_ping
  easter_eggs:
    secret_shake: pet_dance
    upside_down_3s: pet_sleep
    rapid_3x_tap: surprise
```

- Power pins hard-blocked in firmware regardless of allowlist.
- Every IO tool-call logged to `~/.hermes/logs/esp_hermes.log`.
- IMU debounce: on-device low-pass + threshold, N stable samples before emit.

---

## 9. Phased Rollout

| Phase | Deliverable |
|---|---|
| 1 | PTT + always-on voice + pet on LCD (idle/run/done) |
| 2 | IMU wake + tap/shake toggle + pet motion-coupling (lean/dizzy) |
| 3 | IO-tool layer (GPIO/I2C/PWM) + safety allowlist + audit |
| 4 | Movement-triggered tasks + easter eggs + video bursts |
| 5 | Motor control — Hermes has a body (nod/shake in 3D) |

Ship Phase 1 first. Never block on later phases.

---

## 10. Build Checklist (hardware arrived)

- [ ] Clone ESP-Claw, fork `edge_agent` → `esp_hermes_client`
- [ ] Strip LLM client from firmware
- [ ] Build WS client + capabilities report
- [ ] Audio capture/play (I2S + opus)
- [ ] LCD petdex renderer + GIF burst
- [ ] IMU motion → events + pose
- [ ] IO-tool handlers (gpio/i2c/pwm/uart)
- [ ] Gateway: `esp_hermes.py` adapter + WS hub
- [ ] IO tools `tools/esp_io.py` + registry
- [ ] `config.yaml` allowlist + gestures + easter eggs
- [ ] Tunnel/gateway auth (Cloudflare or VPN, basic auth)
- [ ] Verify PTT end-to-end
- [ ] Verify always-on VAD
- [ ] Verify pet-state push → LCD
- [ ] Verify IO tool-call (e.g. blink LED on pin 13)
- [ ] Select pet via `hermes pets`

---

## 12. Offline / Degrade Gracefully

WiFi/tunnel drop must NOT brick the device.

- **Reconnect logic:** WS auto-reconnect with exponential backoff (1s→30s).
- **Local fallback:** pet stays in `idle`; on-device easter eggs + audio cues still work offline.
- **Cached responses:** last N agent replies cached on ESP (NVS/SPIFFS) → replay if asked same question offline (optional).
- **Heartbeat:** ESP pings every 30s; if no `pong` for 90s → show "disconnected" glyph, retry.
- **No hang:** audio capture/upload has timeout; never block main loop on dead socket.

## 13. Proactive Push (Gateway → Device, unsolicited)

Device is a real agent, not a walkie-talkie. Gateway can push events without user prompt:

- Cron job finished → `pet_state: done` + optional TTS "Task X done".
- Alert / threshold breach → `pet_state: error` + buzz.
- Inbound message from another channel (Telegram) → ESP shows notification glyph.
- `/notify` style: any Hermes output can target `device_id` proactively.

Implementation: `EspHermesAdapter.send()` callable from cron/agent context, not
only from conversation loop. Device must distinguish **proactive** vs **reply**
(audio auto-play vs. wait-for-ack).

## 14. Session Isolation per Device

- Each ESP `device_id` maps to its **own Hermes session** (isolated context/memory).
- Shared user profile + global memory, but per-device conversation history.
- Config: `esp_hermes.devices.<id>.session: isolated|shared`. Default isolated.
- Prevents context bleed between your ESP, Telegram, Desktop.

## 15. OTA + Power Management

- **OTA:** use ESP-IDF `esp_https_ota` or ESP-Claw Board Manager. Gateway can
  push firmware URL → device self-updates (with rollback partition).
- **Deep-sleep:** if always-on VAD unused + idle > N min → ESP sleeps, RTC wakes
  on button/IMU interrupt.
- **Battery:** read battery ADC → push `%` to gateway; auto-dim LCD at low %.
- **Auto-dim:** LCD brightness drops in `idle` to save power.

## 16. Local Audio Cues (on-device, no gateway)

Non-verbal feedback, instant, works offline:

| Cue | Trigger | Sound |
|---|---|---|
| Wake chime | connect / wake | short rise tone |
| Busy | `run` state | soft tick |
| Done | `done` state | pleasant 2-note |
| Error | `error` state | low buzz |
| Mode switch | PTT↔VAD | click |

Synthesized via tone generator (no TTS needed). Config: `esp_hermes.audio_cues: true`.

## 17. TLS / Hardening

Permanent hardware access (relays/motors) = attack surface.

- **WSS only:** TLS for all WS traffic (no plaintext).
- **Token:** per-device `device_token` in URL query; rotate via gateway API.
- **Replay protection:** gateway rejects stale `nonce`/`ts` on tool_calls.
- **TLS pinning:** ESP pins gateway cert fingerprint (optional, stronger).
- **Rate-limit + allowlist** (see §8) are the physical safety net.
- **Audit:** all IO + connection events → `~/.hermes/logs/esp_hermes.log`.

## 18. ESP Commands (slash-style, like Telegram)

The ESP needs its own command surface — typed or gesture/voice-triggered,
mirroring Telegram `/commands`.

**Input methods:**
- Voice: "Hermes, Befehl <x>" → STT → parsed as command
- Serial/companion app: type command over USB/BLE
- Gesture macro: long-press + shake = open command palette on LCD

**Command set (v1):**
| Cmd | Action |
|---|---|
| `/mode ptt\|vad` | switch input mode |
| `/pet <slug>` | change pet (uses `hermes pets`) |
| `/display pet\|tui` | toggle LCD mode |
| `/mute` `/unmute` | audio cues on/off |
| `/status` | device → gateway → returns battery, wifi, session |
| `/sleep` | deep-sleep now |
| `/wake` | (interrupt-driven, not command) |
| `/tasks` | list active cron/agent tasks |
| `/notify <msg>` | push note to another channel (Telegram) |
| `/gpio <pin> <H\|L>` | direct IO (subject to allowlist + approval) |
| `/i2c <addr> <reg>` | read sensor |
| `/reset` | clear device session |
| `/help` | list commands on LCD |

**Gateway side:** commands parse in `EspHermesAdapter` before agent loop
(same as Telegram slash routing in `gateway/run.py`). IO commands gated by §8
allowlist + approval.

## 11. Open Questions (resolve at hardware arrival)

- [ ] Audio codec (Opus vs PCM) — bandwidth vs ESP decode cost
- [ ] WS msg schema finalize (above is draft)
- [ ] Pet frame format for LCD (sixel? custom sprite? downscaled 192×208)
- [ ] Power: always-on VAD battery impact (Stick S3 has battery?)
- [ ] Wake-word for always-on (optional, on-device)
- [ ] Does Stick-S3 have PSRAM for video bursts? Confirm RAM budget
- [ ] Gateway WS endpoint auth (token in query vs header)
