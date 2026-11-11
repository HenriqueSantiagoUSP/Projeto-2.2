# System-wide state flags (network, clock, MQTT, CAN) in one FreeRTOS event group.
idf_component_register(
    SRCS "sys_events.c"
    INCLUDE_DIRS "include"
)
