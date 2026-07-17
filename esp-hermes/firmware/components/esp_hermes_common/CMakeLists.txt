# Interface component: shared esp_hermes.h declarations (types, pins, protocol).
# No .c here — esp_hermes.c lives in main and implements these.
idf_component_register(
    SRCS
    INCLUDE_DIRS
        "include"
    REQUIRES
        espressif__cjson
        esp_websocket_client
)
