# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20.0)

add_library(cs_common STATIC antenna.c ble_callbacks.c ble_scanning.c
                             cs_initiator.c cs_step_parse.c addr_utils.c)
target_include_directories(cs_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cs_common PRIVATE zephyr_interface)