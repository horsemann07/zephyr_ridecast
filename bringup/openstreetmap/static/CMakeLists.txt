cmake_minimum_required(VERSION 3.20.0)

# Standalone bring-up application; the RideCast root CMakeLists.txt is unchanged.
find_package(Zephyr REQUIRED HINTS "${CMAKE_CURRENT_LIST_DIR}/../../zephyr_workspace/zephyr")
project(ridecast_osm_display_test LANGUAGES C)

target_sources(app PRIVATE src/main.c)
