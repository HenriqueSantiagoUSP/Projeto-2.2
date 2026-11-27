cmake_minimum_required(VERSION 3.22)

# Hardware test of the single PCB (firmware/hwtest/README.md). Only the components it reads the
# board with: the shared ones (pins, CAN, LED, RTOS helpers) and the wearable's MPU-6050 driver.
# No Wi-Fi, no MQTT, and no fall pipeline (so no esp-dsp download).
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../common/pcb"
    "${CMAKE_CURRENT_LIST_DIR}/../common/can"
    "${CMAKE_CURRENT_LIST_DIR}/../common/indicators"
    "${CMAKE_CURRENT_LIST_DIR}/../common/protocol"
    "${CMAKE_CURRENT_LIST_DIR}/../common/rtos_util"
    "${CMAKE_CURRENT_LIST_DIR}/../common/sys_events"
    "${CMAKE_CURRENT_LIST_DIR}/../gadget/components/mpu6050"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(MINIMAL_BUILD ON)
project(hwtest)
