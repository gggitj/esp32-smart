# Current reference board: sensors, LCD, power policy and authenticated Pi LAN/BLE.
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../shared/module_data_service"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/pi_sensor_device"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/lvgl")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(s3_pogo_device)
