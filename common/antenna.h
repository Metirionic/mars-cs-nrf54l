/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Antenna configuration lookup tables for Channel Sounding
 */

#ifndef ANTENNA_H
#define ANTENNA_H

#include <stdint.h>
#include <zephyr/bluetooth/cs.h>

uint8_t antenna_get_config_for_role(enum bt_conn_le_cs_role role);
uint8_t antenna_get_peer_mask(void);

#endif /* ANTENNA_H */
