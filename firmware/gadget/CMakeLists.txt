cmake_minimum_required(VERSION 3.22)

# Components shared with the room hub (firmware/common): contract, network, MQTT, CAN, RTOS helpers, LED/buzzer.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../common")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(MINIMAL_BUILD ON)
project(gadget)
