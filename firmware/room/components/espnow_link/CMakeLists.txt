idf_component_register(
    SRCS "espnow_link.c"
    INCLUDE_DIRS "include"
    REQUIRES "protocol"
    PRIV_REQUIRES "esp_wifi" "rtos_util"
)
