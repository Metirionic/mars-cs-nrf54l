# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(initiator)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common cs_common)

target_sources(app PRIVATE src/main.c src/serialize.c)
target_include_directories(app PRIVATE src)
target_link_libraries(app PRIVATE cs_common)