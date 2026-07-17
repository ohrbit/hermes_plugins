# main/CMakeLists.txt — registers the esp_hermes_client component sources.
# Note (ESP-IDF v5+): esp_websocket_client is pulled via idf_component.yml
# (Component Manager). i2c/ledc/uart/spi_flash live inside `driver`, so they
# are NOT separate REQUIRES entries. esp_adc is its own component (esp_adc/...).
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
        "lcd_viz.c"
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
        fatfs
        espressif__cjson
        esp_websocket_client
        esp_driver_ledc
        esp_driver_i2c
        esp_driver_uart
        esp_driver_i2s
        esp_lcd
        esp_driver_spi
)

# Audio codec (Opus vs PCM) — spec §11. Default PCM (no external lib yet).
# To use Opus: add a prebuilt libopus component or idf_component.yml dep.
