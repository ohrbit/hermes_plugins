---
name: esp-hermes-channel
description: "Build the ESP32-S3 (M5Stack) as a first-class Hermes voice channel — like Telegram/Desktop, not a standalone agent. Covers architecture, audio pipeline, 2-mode design (always-on VAD + push-to-talk), and petdex LCD rendering via gateway state-push."
version: 0.1.0
author: Hermes Agent
license: MIT
metadata:
  hermes:
    tags: [esp32, m5stack, hermes, voice-channel, iot, petdex, gateway]
---

# ESP-Hermes Channel

Turn an ESP32-S3 (M5Stack) into a **Hermes voice channel** — parity with Telegram/Desktop clients, NOT a standalone agent. Hermes (gateway) does STT + reasoning + tools + TTS + pet-state; the ESP does I/O only.

## Architecture

```
ESP32-S3 (esp-hermes firmware, C/Arduino)
  mic ──audio──▶ Hermes Gateway (:9119, like Telegram channel)
                      │
                      ├─ STT (local faster-whisper / groq)
                      ├─ Agent reasoning + tools + memory
                      ├─ TTS (Edge TTS) ──audio──▶ speaker
                      └─ Pet-State push (idle/run/review/error/done) ──▶ LCD
```

- ESP is a **gateway client**, not an LLM client. Do NOT use ESP-Claw's LLM mode — that makes the ESP a standalone agent. We want Hermes as the brain.
- Channel parity table:

| Feature | Desktop | Telegram | ESP32 |
|---|---|---|---|
| Send text/audio | ✓ | ✓ | ✓ (button→mic) |
| Agent replies | ✓ | ✓ | ✓ (TTS→speaker) |
| Pet on display | ✓ | ✗ | ✓ (LCD) |
| Tool-calls | ✓ | ✓ | ✓ (via gateway) |

## Full Hardware Access (Hermes-controlled IoT Node)

ESP is a **gateway client**, so the agent gets full tool-call access to the S3's IO — the device becomes Hermes' physical extremity.

| IO | Access via Hermes | Example |
|---|---|---|
| **GPIO** (digital in/out) | Tool-Call → ESP sets/reads pin | relay, LED |
| **I2C / SPI** (sensors, displays) | Tool-Call → Bus-Read/Write | external sensors |
| **ADC** (analog, voltage, temp) | Tool-Call → `analogRead` | battery, thermistor |
| **PWM** (LEDs, servos, motors) | Tool-Call → `ledcWrite` | dimming, servo |
| **UART** (other MCUs) | Tool-Call → Serial passthrough | chain devices |
| **Button/Touch** | Input-Event → triggers Hermes | PTT, mode toggle |
| **IMU — Accelerometer + Gyroscope** | Tool-Call `esp_imu_read` **+ Event-Driven triggers** | motion/orientation/gesture |
| **LCD** | Pet-State **+** arbitrary render output | pet + status |
| **mic/speaker** | Voice-Channel (as discussed) | talk to Hermes |

### IMU (Accelerometer + Gyroscope) — special case

M5Stack S3 includes a 6-axis IMU (accel + gyro). Not just readable — **event-driven**:

- **Polled:** `esp_imu_read` → returns accel/gyro vector on demand ("what's the orientation?").
- **Event-Driven (recommended):** on-device motion detection triggers Hermes events without a prompt:
  - **Shake** → toggle always-on / PTT mode
  - **Tap** → PTT trigger (alt to button)
  - **Orientation flip** → context switch ("device picked up" → wake)
  - **Stillness timeout** → return to `idle` pet state
- ESP runs lightweight motion filter (threshold/low-pass) so it doesn't spam the gateway with noise.

Mirrors ESP-Claw's "Event Driven" feature — but the *brain* stays Hermes (gateway), not on-device Lua.

### Design Principles (user-driven — learned this session)

- **Device = Hermes channel, not standalone agent.** ESP speaks to the gateway (WS); Hermes is the brain. Do NOT use ESP-Claw's LLM mode.
- **Reuse vendor SDK/components** (ESP-Claw `common`/`edge_agent`) instead of rewriting from scratch. Strip the LLM client, keep hardware/IMU/event layers.
- **Start small, phased** (voice-pet first, body later). Ship Phase 1, grow incrementally. User explicitly: "wir fangen klein an".
- **MCP direction caution:** a device's MCP adapter usually makes the *device* the MCP *client* (it calls servers), so it does NOT grant Hermes access to the device. Don't assume reverse direction.
- **Verify spec/benchmark numbers before stating them.** User corrects confident wrong claims — e.g. 200 TOPS NPU ≠ faster LLM than his S24 (real token speed wins); and LMArena ELO for gemma-4-31b (1451) was wrongly attributed to the 2B mobile variant. Check the number matches the exact model/size before claiming.

## Two Modes (REQUIRED)

1. **Push-to-talk (PTT):** Hold button → record → release → upload. Pet shows `recording` state while held.
2. **Always-on (VAD):** Continuous mic, on-device Voice Activity Detection triggers upload only on speech. Pet shows `listening` idle state; `run` while Hermes processes.

Mode toggle: button short-press cycles modes, or gateway config push. Persist mode in ESP NVS.

## Audio Pipeline

- Upload: raw PCM or OGG/Opus (M5 mic → encode → POST to gateway `/voice` or webhook).
- Downlink: TTS audio streamed back → ESP decodes → speaker.
- Codec decision pending hardware arrival (I2S mic + MAX98357 or built-in M5 speaker).

## Pet on LCD

- petdex renderer runs on the **gateway** (Hermes side), not ESP.
- Gateway pushes `pet_state` + frame data over the same channel socket (WebSocket recommended).
- ESP LCD renders frames for current state. States: `idle`, `run`, `review`, `error`, `done`.
- Pet slug chosen by user (same gallery as desktop — `hermes pets list`).

### petdex scope vs. custom IMU layer (IMPORTANT)

petdex (verified): 3,317+ pets, Codex sprite format, 192×208 grid. Pets react to **agent-lifecycle states** (`idle`/`run`/`review`/`error`/`done`) — this is the foundation and works out-of-the-box for the ESP LCD.

**Gap:** petdex does NOT react to physical device motion. IMU coupling (pet leans on tilt, dizzy on shake) is NOT a native petdex feature.

Two integration paths:
- **Path A (safe, compatible):** IMU → gateway maps motion to existing petdex states (`run`=busy, `done`=content). Always compatible, no custom frames.
- **Path B (rich, custom):** add custom pet states (`tilt`, `shake`, `stretch`) — requires pets whose atlases contain those frames. Needs custom pet or petdex atlas extension.

**Decision:** petdex = foundation (agent states). Physical IMU coupling = custom layer on top (Path A for v1, Path B later). The gateway controls the state-push, so we own the mapping.

### Video on state change (optional, theoretical)

Beyond sprite pets, the LCD can play **short video clips** on state transitions:
- Gateway pushes `play_video: <clip-id>` instead of (or alongside) `pet_state`.
- Feasible formats on ESP32-S3: MJPEG (TJpgDec per-frame), animated GIF, or raw RGB frames. Avoid MP4/H.264 (no hardware decoder on S3).
- Resolution cap: ∼QVGA (320×240) or the Stick-S3 native panel. Keep clips 1–3s.
- **Best mix:** sprite pet as persistent idle mode (cheap, smooth); video burst only on event (`error`→glitch clip, `done`→celebrate clip).
- Storage: clips on SD/SPIFFS or streamed from gateway over WiFi.

## Transport / Connectivity

- ESP connects to Hermes gateway over WiFi (HTTP or WebSocket).
- Gateway must be reachable: Cloudflare tunnel or VPN (same as S24 setup).
- Auth: API key / basic auth in front of endpoint.
- Recommended: WebSocket for bidirectional (audio up + state/TTS down) to avoid polling.

## IO-Tool Layer (Channel Registration)

On connect, the ESP **registers its IO tools** with the gateway so Hermes sees them like any other tool. Define a stable schema:

```json
{
  "tool": "esp_gpio_set",
  "device": "<esp-id>",
  "params": { "pin": 12, "state": "HIGH" }
}
{
  "tool": "esp_gpio_read", "params": { "pin": 14 } }
{
  "tool": "esp_adc_read",  "params": { "pin": 36, "atten": 11 } }
{
  "tool": "esp_pwm_set",   "params": { "pin": 13, "duty": 2048, "freq": 5000 } }
{
  "tool": "esp_i2c_read",  "params": { "addr": 64, "reg": 0, "len": 2 } }
{
  "tool": "esp_i2c_write", "params": { "addr": 64, "reg": 0, "data": [1,2] } }
{
  "tool": "esp_uart_send", "params": { "data": "AT+CMD\r\n" } }
{
  "tool": "esp_imu_read",  "params": { "axis": "all" } }
```

- Hermes routes these as normal tool-calls; the gateway serializes → WebSocket → ESP → Arduino handler.
- ESP replies with JSON result (value / ack / error).
- **Dynamic discovery:** ESP sends a `capabilities` message on connect so the gateway knows which pins/peripherals exist (avoids hardcoding per-device).

## Safety (CRITICAL — full HW access = physical consequences)

Because Hermes can move motors, flip relays, fire PWM, the channel needs guardrails:

1. **Pin Allowlist** — `config.yaml` under `esp_hermes.allowed`:
   ```yaml
   esp_hermes:
     devices:
       stick-s3:
         allowed_pins: [12, 13, 14, 36]     # only these are callable
         allowed_i2c: [0x40, 0x68]           # sensor addresses only
         blocked_pins: [0, 1, 2, 3]          # boot/flash/UART0 never exposed
   ```
2. **Destructive-action approval** — mirror `approvals.mode`. Critical IO (relay, motor PWM, any `gpio_set` on allowlisted power pin) prompts the user before execution unless `esp_hermes.auto_approve_safe: true`.
3. **Rate-limit** — cap `esp_pwm_set` / `esp_gpio_set` calls per second to prevent strobe / burn-out loops.
4. **Power pins protected** — never expose EN, MTDI, GPIO0/1/2/3 (boot/flash). Hard-block in firmware regardless of allowlist.
5. **IMU event debounce** — on-device low-pass + threshold filter; only emit `tap`/`shake` after N stable samples (no gateway spam).
6. **Audit log** — every IO tool-call logged to `~/.hermes/logs/esp_hermes.log` with timestamp + device + pin + value.

## Physical Interaction Beyond Wake (Embodied Hermes)

The device is not just an input surface — **movement in 3D space is meaningful**. Hermes gets a "body" via the IMU + motor control. Start small, expand later.

### Pet Animation ↔ Motion Coupling

The pet on the LCD must **react to physical movement**, not just agent state:

| Physical action | Pet reaction | Optional task trigger |
|---|---|---|
| Lift / pick up | pet "wakes up", stretches | none (ambient) |
| Shake | pet gets dizzy / spins | toggle mode |
| Tilt left/right | pet leans, looks curious | none |
| Set down gently | pet settles to `idle` | none |
| Fast spin | pet excited 🌀 | easter egg |
| Flip upside down | pet confused 🙃 | none |

→ IMU orientation vector drives pet **pose/animation**, not just events. Smooth interpolation (not snapping) so the pet "feels" alive.

### Movement-Triggered Tasks

Beyond wake: specific gestures = specific Hermes actions (user-definable mapping):

- **Double-tap + tilt** → start a named cron/ritual
- **Figure-8 motion** → "summarize my day" task
- **Knock on table (IMU shock)** → quick status ping

These are event-driven tool-calls, same mechanism as mode-toggle, but **mapped to user tasks** via config.

### Easter Eggs

Motion-based hidden behaviors (fun + delight):

- **Secret shake pattern** → pet does a dance, plays a sound
- **Upside-down hold 3s** → pet "sleeps", LCD dims
- **Rapid 3x tap** → surprise animation

Keep them on-device (cheap, no gateway) so they feel instant.

### Motor Control (Hermes gets a body)

Later phase: attach a motor/servo/RGB — Hermes can **move** the device or its surroundings:

- `esp_motor_set` → rotate / nudge / point
- Physical "nod" on task complete, "shake" on error (mirrors pet states in 3D)
- Turns the channel from a passive speaker into an **embodied agent**

### Phased Rollout (start small)

1. **Phase 1 (first boot):** PTT + always-on voice + pet on LCD (idle/run/done).
2. **Phase 2:** IMU wake + tap/shake mode-toggle + pet motion-coupling (lean/dizzy).
3. **Phase 3:** IO-tool layer (GPIO/I2C/PWM) + safety allowlist.
4. **Phase 4:** movement-triggered tasks + easter eggs.
5. **Phase 5:** motor control — Hermes has a body.

Never block Phase 1 on later phases. Ship the voice-pet first, grow the body incrementally.

## Open Questions (resolve at hardware arrival)

- [ ] Audio codec (Opus vs PCM) — bandwidth vs ESP decode cost
- [ ] State-push protocol (WS msg schema) — define with gateway team
- [ ] Pet frame format for LCD (sixel? custom sprite? downscaled 192×208)
- [ ] Power: always-on VAD battery impact
- [ ] Wake-word for always-on mode (optional, on-device)

## Build Checklist (when hardware arrives)

- [ ] Flash esp-hermes firmware (C/Arduino, WiFi + I2S + LCD)
- [ ] Point ESP at gateway WebSocket URL + auth
- [ ] Verify PTT: button→mic→STT→TTS→speaker
- [ ] Verify always-on: VAD trigger
- [ ] Wire pet-state push → LCD render
- [ ] Select pet slug via `hermes pets`
- [ ] Tunnel/gateway auth hardened

## Development Workflow (proven this session)

How the project was actually scaffolded — reuse for future builds:

1. **Kanban board** `esp-hermes` (Hermes `kanban`) — 7 cards map to spec sections
   (gateway-adapter / ws-hub / io-tools / config-safety / commands / firmware-draft / repo-scaffold).
   Each card tagged with `esp-hermes-channel` skill so JIT workers load context.
2. **JIT agents on Modal** build the gateway-side code (Python) in parallel — firmware is
   **draft-only** until hardware arrives (no flash/test possible without the device).
3. **Repo layout:** `ohrbit/hermes_plugins` → subfolder `esp-hermes/` (NOT hermes_skills).
   Structure: `gateway/`, `tools/`, `firmware/`, `config/`, `references/`,
   hand-authored `README.md` (use `github-readme-authoring` skill — no generators),
   `LICENSE` (MIT), `implementation-spec.md`.
4. **GitHub push without `gh`:** `gh` CLI is NOT installed in this env. Use
   `GITHUB_TOKEN` from `~/.hermes/.env` + curl API + `git remote set-url origin
   https://$GITHUB_TOKEN@github.com/<repo>.git`. Verify push via
   `GET /repos/<repo>/contents/<path>` (expect 200).
5. **Tunnel-Derby lesson applies:** when dispatching JIT workers, pass
   `skills=[esp-hermes-channel]` and confirm Modal RAM before fan-out.

### Kanban / JIT dispatch — exact commands that worked (learned the hard way)

- **Board + cards:** `hermes kanban --board <name> create "Title" --body "..." --skill esp-hermes-channel`
  (the `--board` flag goes BEFORE the subcommand; `--title`/`--board` AFTER it error out).
- **Assign before dispatch:** tasks must be ASSIGNED to a *spawnable* profile or dispatch
  skips them as "unassigned". `hermes kanban --board <name> assign <task_id> <profile>`.
- **`jit-esp` profile crash loop:** a custom-named worker profile (`jit-esp`) crashed at
  spawn on EVERY task (pid not alive, gave_up). The dispatcher auto-**promoted** them to
  `default` (which is spawnable on Modal) and they completed. Lesson: assign to `default`,
  not a made-up profile name, unless that profile is a real spawnable worker.
- **Dispatch:** `hermes kanban --board <name> dispatch --max 3` (flag order: `--board` first).
  `--max` caps concurrent spawns. Remaining ready tasks get picked up by the gateway ticker
  (60s) automatically as workers free up.
- **Local branch chaos:** after repeated `git checkout`/`stash` the local clone's `main`
  showed the wrong file (collection root README instead of `esp-hermes/README.md`). When
  patching a file already on GitHub, the robust path is: fetch via Contents API
  (`GET /repos/<repo>/contents/<path>`), write locally, then PUT back with the returned
  `sha` + `"branch":"main"`. This avoids branch/ref mismatches entirely.

### Agent-install (gateway side) — user preferred flow

User does NOT want to install the gateway manually. The README must let an agent install
it from a URL alone:

```bash
hermes plugins install https://github.com/ohrbit/hermes_plugins
hermes plugins enable esp-hermes
hermes config set gateway.platforms.esp_hermes.enabled true
hermes gateway restart
```

`hermes plugins install <url>` clones the collection and auto-discovers
`esp-hermes/plugin.yaml` inside it. **BUT** the manual human install steps must STILL
stay in the README (user rule: manual steps always present; 3rd-party refs always linked).

### Firmware status (updated at hardware arrival 2026-07-14)

- Firmware is now **structurally complete** — all 10 `.c` modules written and pinned to
  the real Stick S3 (K150) pin map (see `references/sticks3-pinmap.md`).
- Missing modules that were added: `esp_hermes_client.c` (app_main), `imu_motion.c/.h`
  (BMI270 + gestures), `io_tools.c/.h` (GPIO/I2C/PWM/ADC/UART gated by pin-safety).
- **Still not ESP-IDF-built** — host lacks disk for the toolchain. User builds on his own
  ESP-IDF v6 (APIs target v5.2; mostly compatible). Expect first-build errors in ES8311
  init / ST7789 timing / BMI270 registers — normal for fresh hardware.
- Build command: `idf.py set-target esp32s3 && idf.py build && idf.py flash monitor`.
  Hold reset to enter download mode.

## Pitfalls

- Don't reuse ESP-Claw LLM mode — that bypasses Hermes brain.
- Pet rendering belongs on gateway, not ESP (ESP lacks petdex sprites).
- Always-on mode needs on-device VAD or it streams silence constantly (bandwidth + cost).
- `gh` CLI unavailable in this env — use `GITHUB_TOKEN`+curl API, not `gh` commands.
- Don't push the whole cloned upstream repo; `git add <subfolder>/` only (a `git fetch`
  of an existing repo pulls ALL branches/files — stage just your subdir).
- Kanban `create` syntax: `hermes kanban --board <name> create "<title>" --body "..." --skill <skill>`,
  NOT `--title`/`--board` flags after subcommand (that errors "unrecognized arguments").
- **Verified Stick S3 (K150) pin map + firmware structural-check notes** live in
  `references/sticks3-pinmap.md` — IMU is BMI270 (not MPU6886), LCD is ST7789P3 135×240
  (not GC9A01), ES8311 audio @0x18, buttons G11/G12. Read it before wiring firmware.
- **Firmware Draft pitfall:** CMakeLists may reference `.c` files not yet in the repo.
  Run a structural check (every SRCS entry exists + every called symbol is declared) before
  asking the user to flash — catches missing modules without a full ESP-IDF toolchain.
- **ESP-IDF v5/v6 build breakage (real errors hit 2026-07-14, user on v6.0.2):**
  Components moved out of core / removed — each fails cmake with "Failed to
  resolve component 'X'": `esp_websocket_client` (→ idf_component.yml
  `^1.0.0`), `json`/cJSON (→ `espressif/cjson` in idf_component.yml; REMOVED
  entirely in v6), `i2c`/`ledc`/`uart`/`spi_flash` (→ all inside `driver`, drop
  from REQUIRES). Legacy `adc1_get_raw()` removed → use `esp_adc` oneshot API.
  Flash-size default 2MB but Stick S3 has 8MB → set
  `CONFIG_ESPTOOLPY_FLASHSIZE="8MB"` in sdkconfig.defaults + widen partitions.csv.
  Full transcripts + fixes: `references/esp-idf-build-fixes.md`.
- **STALE `sdkconfig` trap (the actual build-abort cause, 2026-07-14):** `sdkconfig.defaults`
  only sets values for *unset* keys. If the user already ran a build, a local `sdkconfig`
  exists and **overrides** the new 8MB default → partition generation still targets 2MB and
  the build dies at the partitions step. Fix on the user's machine:
  `del sdkconfig` (PowerShell) then `idf.py fullclean && idf.py build` to regenerate from
  `.defaults`. Always tell the user to delete `sdkconfig` after you push sdkconfig.defaults
  changes — don't assume the new default takes effect.
- **Cross-machine edit→push→pull workflow (Linux agent ↔ Win11 builder):** the agent edits
  firmware on the Linux host and pushes via the `github-safe-push` helper (GitHub Contents
  API + returned `sha`), NOT `git commit`. So the host clone's `git status` stays **dirty**
  (edits staged-for-API but uncommitted) even though the files ARE on GitHub `main`. The
  Win11 builder must `git pull origin main` to receive pushed fixes before `idf.py build`.
  Don't tell the user "it's committed" — say "pushed to GitHub, run `git pull`".

### Documentation standard (user rule — non-negotiable)
When writing READMEs for this project (or any hw/fw project), the user requires:
1. **Manual human steps always present**, written for a COMPLETE BEGINNER who has
   never used the tool (e.g. "what is ESP-IDF and why do you need it", per-OS install,
   how to load the environment, download mode, troubleshooting table). An agent-install
   one-liner (e.g. `hermes plugins install <url>`) is ADDED ON TOP, never instead of.
2. **Every 3rd-party reference gets an explicit link** — no bare name drops
   (ESP-IDF docs, M5Stack product page, Espressif USB-UART driver, etc.).
3. The `github-readme-authoring` skill governs README structure; hand-author, never
   auto-generate. Beginner-guide detail lives in `firmware/README.md`
   (see `references/esp-idf-build-fixes.md` for the required sections).
