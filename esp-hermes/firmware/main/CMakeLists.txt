# main/CMakeLists.txt — registers the esp_hermes_client component sources.
idf_component_register(
    SRCS
        "esp_hermes_client.c"
        "esp_hermes.c"
        "nvs_config.c"
        "capabilities.c"
        "audio_capture.c"
        "audio_play.c"
        "audio_codec.c"
        "lcd_pet.c"
        "lcd_tui.c"
        "imu_motion.c"
        "io_tools.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_wifi
        esp_netif
        nvs_flash
        esp_websocket_client
        esp-tls
        esp_adc
        i2c
        spi_flash
        fatfs
        json
)

# DRAFT: the opus codec dependency is not yet wired. When hardware arrives and
# the codec decision (spec §11: Opus vs PCM) is made, add either:
#   - a vendored libopus static lib (prebuilt for xtensa/riscv), or
#   - a component pulling libopus via `idf_component.yml`.
# Until then audio_capture/audio_play compile but encode/decode are stubbed.
