# main/CMakeLists.txt — registers the esp_hermes_client component sources.
# Note (ESP-IDF v5+): esp_websocket_client is pulled via idf_component.yml
# (Component Manager), NOT listed in REQUIRES here. ledc/uart live inside
# the `driver` component, so they are not separate REQUIRES entries.
idf_component_register(
    SRCS
        "esp_hermes_client.c"
        "esp_hermes.c"
        "nvs_config.c"
        "capabilities.c"
        "audio_capture.c"
        "audio_play.c"
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
        esp-tls
        esp_adc
        i2c
        spi_flash
        fatfs
        json
)

# Audio codec (Opus vs PCM) — spec §11. Default PCM (no external lib yet).
# To use Opus: add a prebuilt libopus component or idf_component.yml dep.
