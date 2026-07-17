# ESP-IDF v5 / v6 Firmware Build Fixes (esp-hermes)

First real `idf.py build` of the esp-hermes firmware (user on ESP-IDF v6,
Windows) failed at cmake configure. Errors + fixes below. These are ESP-IDF
v5+ migration issues, not esp-hermes-specific — reuse for ANY ESP32-S3 project.

## Error 1 — `esp_websocket_client` unresolved component
```
CMake Error at .../build.cmake:380 (message):
  Failed to resolve component 'esp_websocket_client' required by component 'main': unknown name.
HINT: The component 'esp_websocket_client' could not be found. ... look out for component in
'https://components.espressif.com' and add using 'idf.py add-dependency' command.
```
**Cause:** In ESP-IDF v5+ `esp_websocket_client` moved OUT of the core IDF into
the **IDF Component Manager** registry. It is no longer a built-in component you
list in `REQUIRES`.

**Fix:** add `firmware/idf_component.yml` (at the project root, sibling of
`CMakeLists.txt`):
```yaml
dependencies:
  esp_websocket_client:
    version: "^1.0.0"
```
The manager auto-downloads it at cmake time. Do NOT list it in `main/CMakeLists.txt`
`REQUIRES` (that's what triggered the error).

## Error 2 — `ledc` / `uart` not found in REQUIRES
Same "unknown name" pattern for `ledc` and `uart`.
**Cause:** In v5+ these are NOT separate components — they live INSIDE the
`driver` component. Listing them in `REQUIRES` errors out.
**Fix:** remove `ledc` and `uart` from `REQUIRES`; keep `driver` (which provides
both `driver/ledc.h` and `driver/uart.h`).

## Error 3 — legacy ADC API removed
`adc1_config_width()` / `adc1_get_raw()` no longer compile in v5+.
**Cause:** legacy ADC driver removed; replaced by the unified `esp_adc` API.
**Fix:** use `esp_adc/esp_adc.h` oneshot API:
```c
adc_oneshot_unit_handle_t unit;
adc_oneshot_unit_init_cfg_t u = { .unit_id = ADC_UNIT_1 };
adc_oneshot_new_unit(&u, &unit);
adc_oneshot_chan_cfg_t c = { .atten = ADC_ATTEN_DB_11, .bitwidth = ADC_BITWIDTH_12 };
adc_oneshot_config_channel(unit, ch, &c);   // ch = gpio_to_adc1_ch(pin)
int raw; adc_oneshot_read(unit, ch, &raw);
adc_oneshot_del_unit(unit);
```
ESP32-S3 ADC1 channel mapping: GPIO1=CH0 … GPIO10=CH9 (i.e. `ch = pin - 1`).

## Error 4 — `json` (cJSON) component removed entirely in v6.0

```text
CMake Error at .../build.cmake:380 (message):
  Failed to resolve component 'json' required by component 'main': unknown name.
```

**Cause:** ESP-IDF **v6.0 removed the built-in `json` (cJSON) component
completely** (it was only deprecated/moved in v5). It is no longer in core and
is NOT auto-provided by anything.

**Fix:**
1. Add to `firmware/idf_component.yml`:
   ```yaml
   dependencies:
     esp_websocket_client:
       version: "^1.0.0"
     espressif/cjson:
       version: "^1.0.0"
   ```
2. REMOVE `json` from `main/CMakeLists.txt` `REQUIRES`.
3. In C code include as `#include "cjson/cJSON.h"` (the managed component
   exposes the `cjson/` prefix). Adjust any bare `#include "cJSON.h"`.

> Note: this is the difference between v5 (json still in core, optionally
> managed) and v6 (json gone from core, MUST use `espressif/cjson`). If you
> target v5.2 you can keep `json` in REQUIRES; for v6 use `espressif/cjson`.

## Error 5 — partition table exceeds configured flash size (2MB default)

```text
Partitions tables occupies 4.0MB of flash (4194304 bytes) which does not fit
in configured flash size 2MB. Change the flash size in menuconfig under the
'Serial Flasher Config' menu.
```

**Cause:** Stick S3 (K150) has **8MB Flash** (ESP32-S3-PICO-1-N8R8). The
default `sdkconfig` flash size is 2MB, but `partitions.csv` was laid out for
more. (The build gets PAST cmake configure and FAILS at the partition-table
generation step — different stage than Errors 1-4.)

**Fix:**
1. In `sdkconfig.defaults` add:
   ```
   CONFIG_ESPTOOLPY_FLASHSIZE="8MB"
   ```
2. Adjust `partitions.csv` to an 8MB budget, e.g.:
   ```
   nvs,      data, nvs,     0x9000,   0x6000,
   phy_init, data, phy,     0xf000,   0x1000,
   factory,  app,  factory, 0x10000,  0x300000,
   storage,  data, fat,     0x310000, 0x4F0000,
   ```
3. Re-run `idf.py build` (changing `sdkconfig.defaults` re-triggers
   configuration; you do NOT need `set-target` again). After this the partition
   table generates and compilation proceeds into the `.c` files.

## Build command sequence (verified)
```bat
idf.py set-target esp32s3
idf.py build
idf.py flash monitor      # hold reset until green LED blinks = download mode
```
First `set-target` does a `fullclean` + regenerates sdkconfig — that's normal.

## After these fixes
Build got past cmake configure. Next expected errors are at the peripheral
layer (ES8311 I2S init, ST7789P3 SPI timing, BMI270 register map) — those are
real-hardware bring-up, fix iteratively on the device. See
`references/sticks3-pinmap.md` for the register notes.

## Beginner doc rule (user preference)
The `firmware/README.md` MUST contain a step-by-step guide for someone who has
NEVER used Espressif tools: what ESP-IDF IS, how to install it (per-OS), how to
load the environment (`export.sh` / ESP-IDF Command Prompt), how to enter
download mode, and a troubleshooting table. Keep it even when an agent-install
path exists. 3rd-party references (ESP-IDF docs, Espressif USB-UART driver) get
explicit links.
