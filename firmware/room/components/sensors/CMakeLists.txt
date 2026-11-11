# The bus (sensor_events) and the drivers stay in one component: the drivers register
# their inputs with the bus, and the bus reports through a listener set by the hub.
idf_component_register(
    SRCS "sensor_events.c" "hcsr501.c" "reedswitch.c" "lm35.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_driver_gpio"
    PRIV_REQUIRES "board" "rtos_util" "esp_timer" "esp_adc" "esp_system"
)
