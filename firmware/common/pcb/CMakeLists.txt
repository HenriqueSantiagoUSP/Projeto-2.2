# Header-only: the pin map of the single PCB shared by both boards (pcb_pins.h), checked
# at compile time. Not named "board": each project has its own board component, which
# picks from this map.
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES "esp_driver_gpio"
)
