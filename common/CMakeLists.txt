# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20.0)

add_library(
  cs_common STATIC
  antenna.c
  ble_callbacks.c
  ble_scanning.c
  cs_initiator.c
  cs_step_parse.c
  cs_watchdog.c
  subevent.c
  addr_utils.c)
target_include_directories(cs_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cs_common PRIVATE zephyr_interface)
# cs_common is a plain CMake library, so it does not inherit the
# add_dependencies(<lib> zephyr_generated_headers) that Zephyr applies to its
# own libraries. zephyr/kernel.h includes the generated heap_constants.h, and
# without this edge a parallel build can compile these sources before that
# header is emitted (#132).
add_dependencies(cs_common zephyr_generated_headers)
target_compile_options(
  cs_common PRIVATE $<$<C_COMPILER_ID:GNU>:-Werror=incompatible-pointer-types>)
