idf_component_register(
    SRCS "espnow_tx.c"
    INCLUDE_DIRS "include"
    REQUIRES "protocol"
    PRIV_REQUIRES "esp_wifi" "esp_timer"
)
