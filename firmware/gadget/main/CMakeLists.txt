# With MINIMAL_BUILD only what main requires is built, so every project component is listed.
# mqtt_link and can are always listed; menuconfig decides whether they are used (GADGET_MQTT,
# GADGET_CAN), and the linker drops them when they are not.
idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES "board" "mpu6050" "fall_pipeline" "signal_proc" "button_gesture" "espnow_tx" "battery"
                  "indicators" "protocol" "rtos_util" "sys_events" "net" "mqtt_link" "can"
                  "esp_driver_gpio" "esp_driver_i2c" "esp_timer" "esp_app_format" "esp_system"
                  "nvs_flash"
)
