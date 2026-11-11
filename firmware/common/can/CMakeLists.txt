# can.c and can_ids.c include each other's headers, so they stay in one component.
# Pins and group come from the board through can_config_t, so both boards share it.
idf_component_register(
    SRCS "can.c" "can_ids.c"
    INCLUDE_DIRS "include"
    REQUIRES "protocol"
    PRIV_REQUIRES "esp_driver_twai" "rtos_util" "sys_events" "esp_timer"
)
