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

#endif /* ANTENNA_H__ */
