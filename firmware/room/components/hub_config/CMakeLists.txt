# hub_config_json.c uses only cJSON and rules.h (host testable); hub_config_nvs.c is the ESP-IDF part.
idf_component_register(
    SRCS "hub_config_json.c" "hub_config_nvs.c"
    INCLUDE_DIRS "include"
    REQUIRES "rules"
    PRIV_REQUIRES "protocol" "nvs_flash"
)
