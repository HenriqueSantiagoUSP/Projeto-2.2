# Header-only: the wearable's pin names (board_pins.h), picked from the single PCB map (pcb).
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES "pcb" "esp_driver_gpio"
)
