# main/CMakeLists.txt — registers the esp_hermes_client component sources.
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
        esp_websocket_client
        esp-tls
        esp_adc
        i2c
        spi_flash
        fatfs
        json
        ledc
        uart
)

# Audio codec (Opus vs PCM) — spec §11. Default PCM (no external lib yet).
# To use Opus: add a prebuilt libopus component or idf_component.yml dep.
