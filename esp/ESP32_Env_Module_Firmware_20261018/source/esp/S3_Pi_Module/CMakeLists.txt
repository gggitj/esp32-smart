# ESP32-S3 pogo device (module side) minimal bring-up.
# Talks to s3_pogo_host over the pogo connector using the LinkIt SDK.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../shared/module_data_service"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/pi_sensor_device")
project(s3_pogo_device)
