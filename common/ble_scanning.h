/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief BLE scanning and Ranging Service UUID filtering
 */

#ifndef BLE_SCANNING_H
#define BLE_SCANNING_H

#include <zephyr/bluetooth/conn.h>

int scan_init(void);

#endif /* BLE_SCANNING_H */
