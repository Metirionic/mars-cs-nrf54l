# SPDX-License-Identifier: MIT
# Throwaway UART-only stream test for the #190 COBS corruption investigation.
cmake_minimum_required(VERSION 3.20.0)

# The board overlay lives in <app>/boards/, and Zephyr only searches
# <root>/boards/ for it -- so the app dir has to be a BOARD_ROOT, the same
# idiom initiator/CMakeLists.txt uses to reach the repo's boards/ dir. This
# must precede find_package(Zephyr): BOARD_ROOT is consumed while locating the
# board, and an app-root overlay is silently ignored.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(uart_stream)

target_sources(app PRIVATE src/main.c)
