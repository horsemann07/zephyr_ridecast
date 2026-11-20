cmake_minimum_required(VERSION 3.20.0)

# Standalone Phase 2B bring-up; leave RideCast root app unchanged.
find_package(Zephyr REQUIRED HINTS "${CMAKE_CURRENT_LIST_DIR}/../../../zephyr_workspace/zephyr")
project(ridecast_osm_interactive LANGUAGES C)

target_sources(app PRIVATE src/main.c)
