# With MINIMAL_BUILD only what main requires is built, so every component is listed.
# The hub's sensor headers (lm35.h, hcsr501.h, reedswitch.h) are only included for their
# constants: the sensors component itself is not built here.
idf_component_register(
    SRCS "main.c"
    PRIV_INCLUDE_DIRS "../../room/components/sensors/include"
    PRIV_REQUIRES "hwtest_checks" "mpu6050" "can" "indicators" "pcb" "protocol" "rtos_util" "sys_events"
                  "esp_driver_gpio" "esp_driver_i2c" "esp_driver_uart" "vfs" "esp_adc" "esp_timer"
                  "esp_hw_support" "esp_system"
)
