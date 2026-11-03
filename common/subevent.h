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

struct cs_local_subevent;

/** Called once per decoded subevent; event storage is reused after return. */
typedef void (*subevent_emit_cb)(SubeventResultEvent_t * local, SubeventResultEvent_t * peer);

/** Decode RAS procedure data and emit each subevent separately, in order.
 * Local headers and step slices must correspond to the peer's RAS subevents.
 * Returns false for malformed or mismatched data; never emits an incomplete pair.
 */
bool subevent_populate(SubeventResultEvent_t *          local,
                       SubeventResultEvent_t *          peer,
                       uint64_t                         local_mac,
                       uint64_t                         peer_mac,
                       const struct cs_local_subevent * subevents,
                       size_t                           subevent_count,
                       struct net_buf_simple *          local_steps,
                       struct net_buf_simple *          peer_steps,
                       enum bt_conn_le_cs_role          role,
                       subevent_emit_cb                 emit);

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
