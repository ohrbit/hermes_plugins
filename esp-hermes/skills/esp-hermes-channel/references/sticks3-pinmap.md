# M5Stack Stick S3 (SKU K150) — Verified Pin Map & Firmware Notes

Verified 2026-07-14 from official m5-docs PinMap (docs.m5stack.com/en/core/StickS3).
Used to wire the `esp-hermes` firmware (firmware/main/esp_hermes.h EH_PIN_*).

## Key facts (corrected wrong assumptions)
- **IMU is BMI270**, I2C addr `0x68` — NOT MPU6886.
- **Display is ST7789P3**, 135×240 — NOT GC9A01.
- **Audio codec ES8311** (I2S), I2C addr `0x18`.
- **8MB Flash + 8MB PSRAM** (octal) — enough for GIF/MJPEG video bursts.
- **Buttons:** KEY1 = G11 (PTT), KEY2 = G12 (mode toggle).
- **Power mgmt:** M5PM1 @ I2C `0x6E` shares the G48/G47 bus with IMU + audio.

## Pin map
| Peripheral | Pins |
|---|---|
| LCD (ST7789P3) | MOSI=G39, SCK=G40, RS(D/C)=G45, CS=G41, RST=G21, BL=G38 |
| IMU (BMI270 0x68) | SCL=G48, SDA=G47, INT=G4 (shared bus) |
| Audio (ES8311 0x18) | MCLK=G18, DOUT(mic)=G14, BCLK=G17, LRCK=G15, DIN(spk)=G16, SCL=G48, SDA=G47 |
| Buttons | KEY1=G11, KEY2=G12 |
| Port.A (HY2.0-4P) | G9, G10 |
| Hat2-bus | G1,G2,G3,G4,G5,G6,G7,G8,G43,G44 |
| Boot/flash straps (never expose) | G0,G1,G2,G3,G45,G46 |

## Flash / download mode
- Hold reset → green LED flashes = download mode.
- Single click = power on / reset. Double click = power off.

## Firmware build verification (no ESP-IDF toolchain on build host)
ESP-IDF install needs ~2-4GB; a small host may lack space. Before asking the
user to flash, run a **structural compile check** (catches the most common
Draft pitfall: CMakeLists references `.c` files that don't exist):

1. Every `SRCS` entry in `main/CMakeLists.txt` exists on disk.
2. Every function the client calls is declared in a header (grep the `.h` files).
3. All `REQUIRES` components are valid for the target ESP-IDF version. For
   **v6**: only `driver, esp_wifi, esp_netif, nvs_flash, esp-tls, esp_adc,
   fatfs` (plus `esp_websocket_client` + `espressif/cjson` via idf_component.yml).
   Do NOT list `i2c`/`ledc`/`uart`/`spi_flash` (they're inside `driver`) or
   `json` (removed in v6 → use `espressif/cjson`). See
   `references/esp-idf-build-fixes.md` for the exact error transcripts.

This caught, in the first Draft, that `esp_hermes_client.c`, `imu_motion.c`,
and `io_tools.c` were missing from the repo while referenced by CMakeLists.
A full `idf.py build` still must run on the user's machine (ES8311 I2S init,
ST7789P3 timing, BMI270 register map will surface real errors there).

## Audio codec decision (spec §11, still open)
Firmware ships PCM stub; Opus needs a vendored libopus static lib or
`idf_component.yml` dep. Default `EH_AUDIO_PCM` until wired.

## Bring-up register notes (first ESP-IDF build will surface these)
- **ES8311** (0x18): I2C-config the codec BEFORE I2S audio works — set clocks,
  enable ADC/DAC, route mic→I2S and I2S→speaker. Init order matters.
- **ST7789P3** (135×240): SPI mode 0, CS active-low, RS=DC pin. Watch column/
  row offset (135×240 is non-square — many examples assume 240×240 and shift).
- **BMI270** (0x68): soft-reset via CMD reg 0x02; enable ACC+GYR via PWR_CTRL
  0x0E; accel range +/-8g → 4096 LSB/g, gyro +/-2000dps → 16.4 LSB/dps.
  INT=G4 can be left unhandled for polling mode.
- **Buttons:** active-LOW with internal pullup. KEY1 = PTT (hold to record),
  KEY2 = mode toggle.
- **ESP-IDF v6** used by user; code targets v5.2 APIs (mostly compatible).
  First build errors in the three blocks above are expected — fix iteratively.

## External links
- [StickS3 docs (m5-docs)](https://docs.m5stack.com/en/core/StickS3)
- [ESP-IDF get-started v5.2](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32s3/get-started/index.html)
- [ESP-Claw (firmware base we fork)](https://github.com/m5stack/ESP-Claw)
