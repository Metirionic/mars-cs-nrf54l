/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Antenna configuration lookup tables for Channel Sounding
 */

#ifndef ANTENNA_H__
#define ANTENNA_H__

#include <stdint.h>
#include <zephyr/bluetooth/cs.h>

uint8_t get_antenna_config(void);
uint8_t get_preferred_peer_antenna(void);
uint8_t get_antenna_config_for_role(enum bt_conn_le_cs_role role);
uint8_t get_preferred_peer_antenna_for_role(enum bt_conn_le_cs_role role);

#endif /* ANTENNA_H__ */
