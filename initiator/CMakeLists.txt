# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

include(FetchContent)
FetchContent_Declare(
  mars-bluetooth-hci
  GIT_REPOSITORY https://github.com/Metirionic/mars-bluetooth-hci.git
  GIT_TAG cd93ae8)
FetchContent_MakeAvailable(mars-bluetooth-hci)

set(MARS_BT_HCI_RUST_DIR ${mars-bluetooth-hci_SOURCE_DIR}/mars-bluetooth-hci)
set(RUST_TARGET "thumbv8m.main-none-eabihf")

list(APPEND CMAKE_PREFIX_PATH ${MARS_BT_HCI_RUST_DIR})
find_package(mars-bluetooth-hci-rust)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(initiator)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common cs_common)

target_sources(app PRIVATE src/main.c src/serialize.c
                           ${CMAKE_CURRENT_LIST_DIR}/../common/rust_callbacks.c)
target_include_directories(app PRIVATE src)
target_link_libraries(app PRIVATE cs_common)
target_include_directories(cs_common
                           PUBLIC ${MARS_BT_HCI_RUST_INCLUDE_DIRECTORIES})

add_dependencies(app build_mars_bt_hci_rust)
target_link_libraries(app PRIVATE ${MARS_BT_HCI_RUST_LINK_LIBRARIES})
