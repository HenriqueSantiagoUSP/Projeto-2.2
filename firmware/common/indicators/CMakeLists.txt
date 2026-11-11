# Status LED (the DevKitC-1 on-board WS2812) and buzzer, shared by the room hub and the wearable.
idf_component_register(
    SRCS "indicators.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_driver_gpio"
    PRIV_REQUIRES "rtos_util" "esp_timer"
)
