/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief CS subevent data parsing into SubeventResultEvent structures
 */

#ifndef SUBEVENT_H
#define SUBEVENT_H

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/net_buf.h>

#include "mars_bluetooth_hci.h"

/**
 * @brief Populate initiator and reflector SubeventResultEvent structures from CS subevent data (RAS).
 *
 * Parses both local and peer step data into SubeventResultEvent structures
 * suitable for feeding into a ranging flow or serializer.
 *
 * @param p_local_event   Output: populated initiator event.
 * @param p_peer_event    Output: populated reflector event.
 * @param local_mac       Local Bluetooth MAC address.
 * @param peer_mac        Peer Bluetooth MAC address.
 * @param p_result        Pointer to the local subevent result header.
 * @param p_local_steps   Net buffer containing local step data.
 * @param p_peer_steps    Net buffer containing peer step data.
 * @param role            CS role (initiator or reflector).
 */
void subevent_populate(SubeventResultEvent_t *                p_local_event,
                       SubeventResultEvent_t *                p_peer_event,
                       uint64_t                               local_mac,
                       uint64_t                               peer_mac,
                       struct bt_conn_le_cs_subevent_result * p_result,
                       struct net_buf_simple *                p_local_steps,
                       struct net_buf_simple *                p_peer_steps,
                       enum bt_conn_le_cs_role                role);

/**
 * @brief Populate SubeventResultEvent structures from local-only CS subevent data (IPT mode).
 *
 * Mirrors subevent_populate() but reads only the local step buffer. In
 * inline PCT (IPT) mode there is no peer step buffer (the reflector's phase
 * contribution is embedded in the local tones). The peer (reflector) event
 * is filled with the same headers as the local event; cs_step_parse_inline()
 * then synthesizes per-step fields with identity PCTs.
 *
 * The antenna path count is taken from the per-procedure
 * bt_conn_le_cs_subevent_result.header.num_antenna_paths so a procedure that
 * negotiates fewer antenna paths than the controller's compile-time max does
 * not cause cs_step_parse_inline to over-read tone_info past the step's real
 * data_len.
 *
 * @param p_local_event   Output: populated initiator event.
 * @param p_peer_event    Output: populated reflector event (identity PCT).
 * @param local_mac       Local Bluetooth MAC address.
 * @param peer_mac        Peer Bluetooth MAC address.
 * @param p_result        Pointer to the local subevent result header.
 * @param p_local_steps   Net buffer containing local step data.
 * @param role            CS role (initiator or reflector).
 */
void subevent_populate_inline(SubeventResultEvent_t *                p_local_event,
                              SubeventResultEvent_t *                p_peer_event,
                              uint64_t                               local_mac,
                              uint64_t                               peer_mac,
                              struct bt_conn_le_cs_subevent_result * p_result,
                              struct net_buf_simple *                p_local_steps,
                              enum bt_conn_le_cs_role                role);

#endif /* SUBEVENT_H */
