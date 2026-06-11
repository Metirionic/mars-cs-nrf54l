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
 * @brief Mapping of (num_antennas - 1, paths_per_antenna - 1) to tone antenna configuration.
 *
 * Indexed by [CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS - 1]
 *           [CONFIG_BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS / CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS - 1].
 * Out-of-range entries are unreachable with valid Kconfig values.
 */
static const uint8_t ANTENNA_MAPPING[4][4] = {
    {BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1,
     BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B2, BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B2,
     BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B2},
    {BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A2_B1,
     BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A2_B2, BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A2_B2,
     BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A2_B2},
    {BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A3_B1,
     BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A3_B1, BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A3_B1,
     BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A3_B1},
    {BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A4_B1,
     BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A4_B1, BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A4_B1,
     BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A4_B1}
};

/**
 * @brief Mapping of (paths_per_antenna - 1) to preferred peer antenna bitmask.
 *
 * Indexed by [CONFIG_BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS / CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS - 1].
 */
static const uint8_t PREFERRED_PEER_ANTENNA_MAPPING[4] = {
    BT_LE_CS_PROCEDURE_PREFERRED_PEER_ANTENNA_1,
    BT_LE_CS_PROCEDURE_PREFERRED_PEER_ANTENNA_1 | BT_LE_CS_PROCEDURE_PREFERRED_PEER_ANTENNA_2,
    BT_LE_CS_PROCEDURE_PREFERRED_PEER_ANTENNA_1 | BT_LE_CS_PROCEDURE_PREFERRED_PEER_ANTENNA_2,
    BT_LE_CS_PROCEDURE_PREFERRED_PEER_ANTENNA_1 | BT_LE_CS_PROCEDURE_PREFERRED_PEER_ANTENNA_2,
};

/**
 * @brief Returns the tone antenna configuration index based on Kconfig antenna/path counts.
 */
uint8_t get_antenna_config(void)
{
    return ANTENNA_MAPPING[CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS - 1]
                          [CONFIG_BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS / CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS - 1];
}

/**
 * @brief Returns the preferred peer antenna bitmask based on Kconfig path counts.
 */
uint8_t get_preferred_peer_antenna(void)
{
    return PREFERRED_PEER_ANTENNA_MAPPING[CONFIG_BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS / CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS -
                                          1];
}
