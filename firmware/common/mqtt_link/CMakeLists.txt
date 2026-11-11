# Not named "mqtt": a project component of that name would override espressif/mqtt.
idf_component_register(
    SRCS "mqtt_link.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES "protocol" "sys_events"
)
