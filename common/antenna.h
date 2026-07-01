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

uint8_t antenna_get_config_for_initiator(void);
uint8_t antenna_get_mask_for_initiator(void);
uint8_t antenna_get_config_for_role(enum bt_conn_le_cs_role role);
uint8_t antenna_get_mask_for_role(void);

#endif /* ANTENNA_H__ */
