# Plain C, no ESP-IDF headers: test_host/ builds the same file on the PC.
idf_component_register(
    SRCS "button_gesture.c"
    INCLUDE_DIRS "include"
)
