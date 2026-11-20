cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../../zephyr_workspace/zephyr)
project(ridecast_onboard_wifi_ap LANGUAGES C)
target_sources(app PRIVATE src/main.c)
