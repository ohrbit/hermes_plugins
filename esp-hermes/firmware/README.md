# ESP-Hermes Firmware (ESP32-S3 / M5Stack Stick-S3)

**Status: DRAFT** — forked from M5Stack ESP-Claw `application/edge_agent`, LLM
client stripped. The ESP is a *gateway client*, not an LLM client: it speaks
WebSocket to the Hermes gateway (`gateway/esp_hermes_ws.py`) and does I/O only.
Hermes is the brain (spec §1, §2).

**Do NOT flash until hardware arrives.** This draft has NOT been compiled or
flashed. Driver init calls are real ESP-IDF APIs; `TODO(hw)` marks board-specific
tuning that resolves at hardware arrival. The WebSocket protocol matches the
gateway hub verbatim (see `esp_hermes.h` — the single source of truth).

## Layout
```
firmware/
  CMakeLists.txt        # top-level ESP-IDF project (project: esp_hermes_client)
  partitions.csv        # partition table (factory app + fat storage for sprites)
  sdkconfig.defaults    # ESP32-S3 defaults (PSRAM, TLS, WiFi, NVS)
  main/
    CMakeLists.txt
    esp_hermes.h/.c     # protocol contract: enums<->strings, pin safety, URL build
    esp_hermes_client.c # app_main + WS client + dispatch loop + modes + heartbeat
    nvs_config.h/.c     # persist device_id/token/mode/pet/display (spec §5,§7)
    capabilities.h/.c   # report pins/peripherals on connect (spec §3.1)
    audio_capture.h/.c  # I2S mic + on-device VAD (spec §7)
    audio_codec.h/.c    # PCM/Opus abstraction (Opus stubbed — spec §11)
    audio_play.h/.c     # I2S speaker + local tone cues (spec §16)
    lcd_pet.h/.c        # petdex sprite cache + blit (spec §6)
    lcd_tui.h/.c        # terminal scrollback + status bar (spec §6.5)
    imu_motion.h/.c     # 6-axis IMU read + debounced gesture events (spec §7.3,§6)
    io_tools.h/.c       # gpio/i2c/pwm/uart/adc exec + pin allowlist + rate-limit
```

## Build (requires ESP-IDF in PATH)
```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build && idf.py flash monitor
```

## What's wired vs. stubbed (DRAFT)
| Area | State |
|---|---|
| WS connect + capabilities handshake | wired to gateway hub schema |
| Message dispatch (in/out) | wired (pet_state/audio/tool_call/tui_line/...) |
| PTT + VAD capture uplink | wired (I2S + energy VAD; Opus = PCM pass-through) |
| TTS downlink + local cues | wired (PCM play; tone synth done) |
| Pet sprite + TUI render | API + cache done; LCD blit = TODO(hw) |
| IMU read + gestures | API + debounce done; sensor driver = TODO(hw) |
| IO tools (gpio/i2c/pwm/uart/adc) | dispatch + safety done; HW drivers = TODO(hw) |
| Reconnect + heartbeat | wired (auto-reconnect + 30s ping / 90s pong) |
| Opus codec | stubbed (PCM) until codec decision (spec §11) |

Full plan: ../references/implementation-spec.md §5, §6, §6.5.
