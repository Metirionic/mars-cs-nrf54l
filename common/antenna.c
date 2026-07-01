/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Antenna configuration lookup tables for Channel Sounding
 */

#include "antenna.h"

/**
 * @brief Mapping of Dev A and Dev B antenna counts to tone antenna configuration.
 *
 * Indexed by [dev_a_antennas - 1][dev_b_antennas - 1].
 */
static const uint8_t ANTENNA_MAPPING[4][4] = {
    {BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1,
     BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B2, BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B3,
     BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B4},
    {BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A2_B1,
     BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A2_B2, BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1,
     BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1},
    {BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A3_B1,
     BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1, BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1,
     BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1},
    {BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A4_B1,
     BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1, BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1,
     BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1},
};

/**
 * @brief Get the number of antennas expected on the peer device.
 *
 * @return Peer antenna count derived from the configured antenna paths.
 */
static uint8_t antenna_get_peer_count(void)
{
    return CONFIG_BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS / CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS;
}

/**
 * @brief Convert an antenna count into a bit mask.
 *
 * @param antenna_count Number of antennas to include in the mask.
 * @return Bit mask with the lowest @p antennas bits set.
 */
static uint8_t antenna_get_mask(size_t antenna_count)
{
    return BIT(antenna_count) - 1;
}

/**
 * @brief Look up the tone antenna configuration for Dev A and Dev B counts.
 *
 * @param antenna_count_a Number of antennas on CS device A.
 * @param antenna_count_b Number of antennas on CS device B.
 * @return Bluetooth LE CS tone antenna configuration value.
 */
static uint8_t antenna_get_config_from_ab(size_t antenna_count_a, size_t antenna_count_b)
{
    return ANTENNA_MAPPING[antenna_count_a - 1][antenna_count_b - 1];
}

/**
 * @brief Get the initiator tone antenna configuration.
 *
 * @return Bluetooth LE CS tone antenna configuration for initiator role.
 */
uint8_t antenna_get_config_for_initiator(void)
{
    return antenna_get_config_for_role(BT_CONN_LE_CS_ROLE_INITIATOR);
}

/**
 * @brief Get the preferred peer antenna mask for initiator role.
 *
 * @return Peer antenna bit mask.
 */
uint8_t antenna_get_mask_for_initiator(void)
{
    return antenna_get_mask_for_role();
}

/**
 * @brief Get the tone antenna configuration for a CS role.
 *
 * @param role Local CS role used to determine Dev A/Dev B ordering.
 * @return Bluetooth LE CS tone antenna configuration for the role.
 */
uint8_t antenna_get_config_for_role(enum bt_conn_le_cs_role role)
{
    const uint8_t local_antenna_count = CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS;
    const uint8_t peer_antennas       = antenna_get_peer_count();

    if (role == BT_CONN_LE_CS_ROLE_REFLECTOR)
    {
        return antenna_get_config_from_ab(peer_antennas, local_antenna_count);
    }

    return antenna_get_config_from_ab(local_antenna_count, peer_antennas);
}

/**
 * @brief Get the preferred peer antenna mask for a CS role.
 *
 * @return Peer antenna bit mask.
 */
uint8_t antenna_get_mask_for_role(void)
{
    return antenna_get_mask(antenna_get_peer_count());
}
