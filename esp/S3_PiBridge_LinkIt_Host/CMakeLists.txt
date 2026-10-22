# ESP32-S3 pogo host (bridge side) minimal bring-up.
# Talks to s3_pogo_device over the pogo connector using the LinkIt SDK.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(s3_pogo_host)
