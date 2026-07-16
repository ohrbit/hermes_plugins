# ESP-Hermes Firmware (M5Stack Stick S3)

**Status: COMPLETE (structurally) — hardware arrived 2026-07-14.** Firmware is
fully wired to the Stick S3 (SKU K150) pin map. Flash + test on real hardware
remaining. ESP is a *gateway client*, not an LLM client: it speaks WebSocket to
the Hermes gateway (`gateway/esp_hermes_ws.py`) and does I/O only.

## Pin Map (verified from m5-docs, Stick S3)

| Peripheral | Pins |
|---|---|
| LCD (ST7789P3 135×240) | MOSI=G39, SCK=G40, RS=G45, CS=G41, RST=G21, BL=G38 |
| IMU (BMI270 0x68) | SCL=G48, SDA=G47, INT=G4 |
| Audio (ES8311 0x18) | MCLK=G18, DOUT=G14, BCLK=G17, LRCK=G15, DIN=G16 |
| Buttons | KEY1=G11 (PTT), KEY2=G12 (mode) |
| Port.A (HY2.0-4P) | G9, G10 |

IMU + Audio share the I2C bus (G48/G47). ES8311 addr `0x18`, BMI270 `0x68`,
M5PM1 power `0x6E`.

## Layout

```
firmware/
  CMakeLists.txt        # top-level ESP-IDF project (project: esp_hermes_client)
  partitions.csv        # partition table
  sdkconfig.defaults    # ESP32-S3 + PSRAM defaults
  main/
    CMakeLists.txt      # registers all 10 sources
    esp_hermes_client.c # app_main: wifi, WS, tasks
    esp_hermes.c/.h     # WS helpers, dispatch, button task, pin map
    nvs_config.c/.h     # persist mode, device_id, token, wifi
    capabilities.c/.h   # report pins/peripherals on connect
    audio_capture.c/.h  # I2S mic capture (PTT/VAD)
    audio_codec.c/.h    # PCM (Opus stub — spec §11)
    audio_play.c/.h     # I2S speaker (TTS downlink)
    lcd_pet.c/.h        # petdex sprite render on LCD
    lcd_tui.c/.h        # terminal scrollback mode
    imu_motion.c/.h     # BMI270 driver + gesture detection
    io_tools.c/.h       # gpio/i2c/pwm/adc/uart/imu handlers (safety-gated)
```

## Build & Flash (requires ESP-IDF v5.2+ in PATH)

```bash
# 1. Install ESP-IDF (if not done):
#    https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32s3/get-started/index.html
. $HOME/esp/esp-idf/export.sh

# 2. Build + flash (device on USB, hold reset to enter download mode)
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py flash monitor

# 3. First boot: set NVS config (device_id, token, wifi) via; see nvs_config.
#    Or provision over USB serial:  (mechanism TBD in nvs_config.c)
```

## Known TODO (real-hardware verification)

- [ ] ESP-IDF build passes (structural check done, no toolchain on build host)
- [ ] ES8311 I2S init + mic capture
- [ ] ST7789P3 LCD init + petdex render
- [ ] BMI270 accel/gyro reads + gesture thresholds
- [ ] WS connect to gateway, capabilities handshake
- [ ] TTS downlink playback
- [ ] NVS provisioning flow (wifi/device_id/token)
- [ ] Opus vs PCM codec decision (spec §11) — currently PCM stub

Full plan: `../references/implementation-spec.md` §5.
