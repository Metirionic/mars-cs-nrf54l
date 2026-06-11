/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Shared BLE address utility functions
 */

#include "addr_utils.h"

/**
 * @brief Convert a BLE address to a 64-bit integer.
 */
uint64_t addr_to_u64(const bt_addr_le_t * p_addr)
{
    uint64_t mac = 0;
    for (int i = 0; i < 6; i++)
    {
        mac |= ((uint64_t)p_addr->a.val[i]) << (8 * i);
    }
    return mac;
}
