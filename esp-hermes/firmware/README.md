# ESP-Hermes Firmware (ESP32-S3 / M5Stack)

**Status: DRAFT** — forked from M5Stack ESP-Claw `application/edge_agent`, LLM client stripped.
The ESP is a *gateway client*, not an LLM client: it speaks WebSocket to the Hermes gateway
(`gateway/esp_hermes_ws.py`) and does I/O only.

Layout:
```
firmware/
  CMakeLists.txt        # top-level ESP-IDF project (project: esp_hermes_client)
  partitions.csv        # partition table
  sdkconfig.defaults    # ESP32-S3 defaults
  main/
    CMakeLists.txt
    esp_hermes.c/.h     # WS client + message dispatch
    audio_capture.c/.h  # I2S mic capture
    audio_codec.c/.h    # opus encode/decode
    audio_play.c/.h     # I2S speaker
    capabilities.c/.h   # report pins/peripherals on connect
    nvs_config.c/.h     # persist mode, device_id, token
```

Build (requires ESP-IDF in PATH):
```bash
idf.py set-target esp32s3
idf.py build && idf.py flash monitor
```

Full plan: ../references/implementation-spec.md §5. **Do not flash until hardware arrives.**
