idf_component_register(
    SRCS "net.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES "sys_events" "esp_wifi" "esp_netif" "esp_event" "esp_timer"
)
