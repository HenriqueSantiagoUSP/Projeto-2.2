# With MINIMAL_BUILD only what main requires is built, so every project component is listed.
idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES "board" "can" "sensors" "rules" "hub_config" "net" "mqtt_link" "espnow_link"
                  "indicators" "protocol" "rtos_util" "sys_events" "esp_driver_gpio" "esp_timer"
                  "esp_app_format" "esp_system" "nvs_flash"
)
