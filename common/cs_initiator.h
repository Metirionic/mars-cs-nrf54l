/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Shared Channel Sounding initiator connection and configuration flow
 */

#ifndef CS_INITIATOR_H
#define CS_INITIATOR_H

#include <stdint.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include "antenna.h"

#if defined(CONFIG_BT_RAS_RREQ) || defined(CONFIG_BT_RAS_RRSP)
#include <bluetooth/services/ras.h>
#endif  // defined(CONFIG_BT_RAS_RREQ) || defined(CONFIG_BT_RAS_RRSP)

/** @brief CS configuration identifier used for procedure setup. */
#define CS_CONFIG_ID 0
/** @brief Number of Mode-0 calibration steps per CS procedure. */
#define NUM_MODE_0_STEPS 3
/** @brief Sentinel value indicating no valid procedure counter. */
#define PROCEDURE_COUNTER_NONE (-1)
/** @brief Per-step framing in the local IPT step buffer: mode | channel | data_len.
 *
 * Hoisted out of the RAS/IPT @c #if below so the walk loop in cs_step_parse.c
 * (compiled unconditionally) can share this single source of truth with
 * @ref LOCAL_PROCEDURE_MEM, instead of hardcoding the literal 3.
 */
#define MARS_CS_IPT_STEP_FRAMING_LEN  3

#if defined(CONFIG_BT_RAS_RREQ) || defined(CONFIG_BT_RAS_RRSP)
/** @brief Memory required for the local step data net buffer (RAS sizing). */
#define LOCAL_PROCEDURE_MEM                                                     \
    ((BT_RAS_MAX_STEPS_PER_PROCEDURE * sizeof(struct bt_le_cs_subevent_step)) + \
     (BT_RAS_MAX_STEPS_PER_PROCEDURE * BT_RAS_MAX_STEP_DATA_LEN))
#else  // defined(CONFIG_BT_RAS_RREQ) || defined(CONFIG_BT_RAS_RRSP)
/* IPT-mode local step buffer sizing.
 *
 * The negotiated CS mode is MAIN_MODE_2 with no sub-mode or with sub-mode-1
 * (which inserts mode-0 steps), so the local buffer only ever holds mode-0 and
 * mode-2 step data. Mode-1 / mode-3 / sounding-sequence RTT are never produced.
 *
 * Mirrors the RAS sizing in bluetooth/services/ras.h, but parameterises it on
 * the controller's antenna-path Kconfig and accounts for the IPT step framing
 * (3 bytes: mode | channel | data_len) instead of the RAS 1-byte mode tag.
 *
 * tone_info count per mode-2 step is (n_ap + 1): the +1 is the extension slot
 * (see cs.h: tone_index = n_ap corresponds to extension slot), matching
 * BT_RAS_STEP_MODE_2_3_ANT_DEPENDENT_LEN(antenna_paths) in ras.h.
 */
#define MARS_CS_IPT_MAX_STEPS_PER_PROCEDURE 256

#define MARS_CS_IPT_MAX_ANTENNA_PATHS CONFIG_BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS

#define MARS_CS_IPT_MODE_0_MAX_LEN \
    MAX(sizeof(struct bt_hci_le_cs_step_data_mode_0_initiator), sizeof(struct bt_hci_le_cs_step_data_mode_0_reflector))
#define MARS_CS_IPT_MODE_2_MAX_LEN                  \
    (sizeof(struct bt_hci_le_cs_step_data_mode_2) + \
     (MARS_CS_IPT_MAX_ANTENNA_PATHS + 1) * sizeof(struct bt_hci_le_cs_step_data_tone_info))

#define MARS_CS_IPT_MAX_STEP_DATA_LEN MAX(MARS_CS_IPT_MODE_0_MAX_LEN, MARS_CS_IPT_MODE_2_MAX_LEN)

#define LOCAL_PROCEDURE_MEM \
    (MARS_CS_IPT_MAX_STEPS_PER_PROCEDURE * (MARS_CS_IPT_STEP_FRAMING_LEN + MARS_CS_IPT_MAX_STEP_DATA_LEN))
#endif  // defined(CONFIG_BT_RAS_RREQ) || defined(CONFIG_BT_RAS_RRSP)

/**
 * @brief Target-specific CS initiator configuration parameters.
 *
 * Passed to cs_initiator_start() to control PHY selection, timing,
 * and subevent sizing. Values differ between test_rack and ranging targets.
 */
struct cs_initiator_config
{
    uint8_t  cs_sync_phy;            /**< CS sync PHY (e.g. BT_CONN_LE_CS_SYNC_1M_PHY). */
    uint8_t  procedure_phy;          /**< Procedure PHY (e.g. BT_LE_CS_PROCEDURE_PHY_2M). */
    uint32_t min_procedure_interval; /**< Minimum interval between CS procedures (in connection events). */
    uint32_t max_procedure_interval; /**< Maximum interval between CS procedures (in connection events). */
    uint32_t min_subevent_len;       /**< Minimum subevent length in microseconds. */
    uint32_t max_subevent_len;       /**< Maximum subevent length in microseconds. */
    uint16_t max_procedure_len;      /**< Maximum procedure length in microseconds. */
};

/**
 * @brief Callback type for ranging data received from peer (realtime or on-demand).
 *
 * @param p_conn            BLE connection reference.
 * @param ranging_counter Ranging counter of the delivered data.
 * @param err             0 on success, negative errno on error.
 */
typedef void (*cs_initiator_ranging_data_cb)(struct bt_conn * p_conn, uint16_t ranging_counter, int err);

/**
 * @brief Callback type for RAS "data ready" notification (on-demand RD mode).
 *
 * @param p_conn            BLE connection reference.
 * @param ranging_counter Ranging counter of the ready data.
 */
typedef void (*cs_initiator_ranging_data_ready_cb)(struct bt_conn * p_conn, uint16_t ranging_counter);

/**
 * @brief Callback type for CS config creation notification.
 *
 * Called after a CS config is successfully created, allowing the caller
 * to save the config struct for later use (e.g., to obtain the role).
 *
 * @param p_config  The negotiated CS configuration.
 */
typedef void (*cs_initiator_config_created_cb)(struct bt_conn_le_cs_config * p_config);

/**
 * @brief Callback type for inline-PCT (IPT) procedure completion.
 *
 * Invoked when a CS procedure completes in IPT mode and local step data is
 * ready for serialization/distance estimation. Not used in RAS mode.
 *
 * @param p_conn  BLE connection reference.
 * @param err     0 on success, non-zero if the procedure was aborted/dropped.
 */
typedef void (*cs_initiator_inline_result_cb)(struct bt_conn * p_conn, int err);

void                                   cs_initiator_set_ranging_data_cb(cs_initiator_ranging_data_cb cb);
void                                   cs_initiator_set_ranging_data_ready_cb(cs_initiator_ranging_data_ready_cb cb);
void                                   cs_initiator_set_config_created_cb(cs_initiator_config_created_cb cb);
void                                   cs_initiator_set_inline_result_cb(cs_initiator_inline_result_cb cb);
int                                    cs_initiator_start(const struct cs_initiator_config * p_config);
struct bt_conn *                       cs_initiator_get_connection(void);
uint32_t                               cs_initiator_get_ras_feature_bits(void);
uint64_t                               cs_initiator_get_local_mac(void);
uint64_t                               cs_initiator_get_peer_mac(void);
struct net_buf_simple *                cs_initiator_get_local_steps(void);
struct net_buf_simple *                cs_initiator_get_peer_steps(void);
int32_t                                cs_initiator_get_local_ranging_counter(void);
struct bt_conn_le_cs_subevent_result * cs_initiator_get_latest_subevent_header(void);
void                                   cs_initiator_give_sem_local_steps(void);
void                                   cs_initiator_give_sem_data_ready(void);
void                                   cs_initiator_take_sem_data_ready(void);

#endif  // CS_INITIATOR_H
