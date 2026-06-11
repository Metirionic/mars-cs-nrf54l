# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(channel_sounding_ras_reflector)

target_sources(app PRIVATE src/main.c
                           ${CMAKE_CURRENT_LIST_DIR}/../common/antenna.c)
target_include_directories(app PRIVATE src ${CMAKE_CURRENT_LIST_DIR}/../common)
