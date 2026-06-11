/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Shared BLE address utility functions
 */

#ifndef ADDR_UTILS_H__
#define ADDR_UTILS_H__

#include <stdint.h>
#include <zephyr/bluetooth/conn.h>

uint64_t addr_to_u64(const bt_addr_le_t * p_addr);

#endif /* ADDR_UTILS_H__ */
