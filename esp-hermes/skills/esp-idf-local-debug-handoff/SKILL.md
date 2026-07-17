---
name: esp-idf-local-debug-handoff
description: "Self-contained prompt for a LOCAL Hermes agent to debug/build ESP-IDF firmware (M5Stack Stick S3) on Windows. Use when the remote agent can't build (no ESP-IDF toolchain/space) and the user wants a local agent to run the error→fix→rebuild loop directly. Includes v6.0 environment activation (idf_cmd_init.bat) which is the critical gotcha."
---

# ESP-IDF Local Debug Handoff (Stick S3 firmware)

## When to use
- Remote Hermes can't build ESP-IDF (no toolchain, no disk space)
- User has ESP-IDF installed locally (Windows, v6.0.x via Espressif IDE)
- Build-debug loop should run on user's machine, not round-trip to remote

## CRITICAL: v6.0 environment activation
The Espressif IDE shortcut runs `idf_cmd_init.bat` first — this sets
`IDF_PATH`, `PATH`, and the Python venv. A bare `idf.py` in a fresh shell
fails ("command not found"). Each agent shell call is fresh, so you MUST
chain activation + build in ONE command:

```bat
call "C:\Espressif\idf_cmd_init.bat" && cd /d C:\hermes_plugins\esp-hermes\firmware && idf.py build
```

If the path differs, find it:
```bat
dir "C:\Espressif\idf_cmd_init.bat"
```

(v5 used `export.bat`; v6 renamed it to `idf_cmd_init.bat`.)

## The handoff prompt (copy verbatim to local Hermes)
```
Du debuggst den Firmware-Build von "esp-hermes" — M5Stack Stick S3 (ESP32-S3)
als Voice/IO-Client für den Hermes AI Gateway. Device verbindet per WebSocket
mit Gateway, capturet Audio, rendert Pet/TUI auf LCD, liest IMU-Gesten,
führt IO-Tool-Calls aus.

UMGEBUNG:
- OS: Windows, ESP-IDF v6.0.2 (Espressif IDE installiert)
- Pfad: C:\hermes_plugins\esp-hermes\firmware  (Repo: ohrbit/hermes_plugins, branch main)
- Du hast PowerShell/CMD-Zugriff. ESP-IDF-Env MUSS geladen werden:
  call "C:\Espressif\idf_cmd_init.bat" && cd /d C:\hermes_plugins\esp-hermes\firmware
  (jeder Build-Befehl braucht das Prefix — sonst "idf.py not found")

BUILD (ein Befehl):
  call "C:\Espressif\idf_cmd_init.bat" && cd /d C:\hermes_plugins\esp-hermes\firmware && idf.py build
  (falls "target not set": set-target esp32s3 voranstellen)

SCHON GEFIXT (nicht erneut anfassen):
- esp_websocket_client + espressif/cjson = Component-Manager-Deps in idf_component.yml
- i2c/ledc/uart/spi_flash in `driver` (kein eigenes REQUIRES)
- json(cJSON) in v6 entfernt -> #include <cjson/cJSON.h>, Dep espressif/cjson
- sdkconfig.defaults: FLASH 8MB (CONFIG_ESPTOOLPY_FLASHSIZE="8MB"),
  LOG level = int (3=INFO), keine ESP_WIFI_*-Symbole (kommen via NVS)
- partitions.csv: 8MB Flash (factory 3MB, storage ~5MB)
- ADC: neue esp_adc oneshot-API (adc1_get_raw in v5+ entfernt)

STICK-S3 PINMAP (nicht ändern außer begründet):
- LCD ST7789P3: MOSI=G39 SCK=G40 RS=G45 CS=G41 RST=G21 BL=G38
- IMU BMI270: SCL=G48 SDA=G47  (I2C addr 0x68)
- Audio ES8311: MCLK=G18 DOUT=G14 BCLK=G17 LRCK=G15 DIN=G16
- Buttons: KEY1=G11 (PTT) KEY2=G12 (mode toggle)

WICHTIG — STALE SDKCONFIG:
Bei Partition-Error "does not fit in configured flash size 2MB":
  del C:\hermes_plugins\esp-hermes\firmware\sdkconfig
  (dann rebuild — sdkconfig.defaults setzt nur Defaults für UNSET-Werte)

AUFGABE:
idf.py build ausführen. FÜR JEDEM Fehler:
1. echten Fehler aus Build-Output lesen (nicht raten)
2. relevantes File in firmware\main\ fixen
3. rebuild
4. wiederholen bis firmware.bin entsteht

CONSTRAINTS:
- esp_hermes.h = WS-Protokoll-Contract mit Gateway. Nur ändern wenn Build es erzwingt.
- Pin-Safety in io_tools.c (eh_pin_is_blocked) NICHT entfernen.
- Device = Gateway-CLIENT, kein LLM drauf. Dünn halten.
- v6-API-Kompatibilität Prio 1. LCD/ES8311/BMI270-Init aus Datasheet ->
  vermutlich erste echte Compile-Fehler dort.
- EDITIEREN LOKAL. Kein git commit/push ohne explizite Erlaubnis.
- Melde was du geändert hast.

WENN BUILD OK: firmware.bin Pfad + Größe nennen, verbleibende TODOs
(ungetestete HW-Init, stubbed Audio-Codec) auflisten.
```

## Pitfalls
- **Each shell is fresh** → always prefix with `call idf_cmd_init.bat && cd ...`
- **Stale sdkconfig** → delete it when flash-size/partition errors persist
- **v6 renames**: `export.bat`→`idf_cmd_init.bat`, `json`→`espressif/cjson`
- **Don't push** → keep user in control of the repo

## Verify
After local agent reports success, ask for the `firmware.bin` path + size,
then optionally commit via the safe-push script on the REMOTE agent side.
