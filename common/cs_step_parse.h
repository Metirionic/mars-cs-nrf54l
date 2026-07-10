/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Shared CS step data parsing into SubeventResultEvent structures
 */

#ifndef CS_STEP_PARSE_H
#define CS_STEP_PARSE_H

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/net_buf.h>

#include "mars_bluetooth_hci.h"

/**
 * @brief Context for step data parsing callbacks.
 */
struct cs_step_parse_context
{
    SubeventResultEvent_t * p_local_event; /**< Output: initiator event being populated. */
    SubeventResultEvent_t * p_peer_event;  /**< Output: reflector event being populated. */
    uint8_t                 step_index;    /**< Current step index within the procedure. */
    uint8_t                 n_ap;          /**< Number of antenna paths in current ranging header. */
};

void cs_step_parse(SubeventResultEvent_t * p_local_event,
                   SubeventResultEvent_t * p_peer_event,
                   struct net_buf_simple * p_peer_steps,
                   struct net_buf_simple * p_local_steps,
                   enum bt_conn_le_cs_role role);

/**
 * @brief Parse local-only step data and populate SubeventResultEvent step fields (IPT).
 *
 * Mirrors cs_step_parse() but reads only the local step buffer. In IPT mode the
 * reflector's phase contribution is embedded in the local tones, so there is no
 * peer step buffer. The peer (reflector) event is synthesized per step with the
 * same metadata as the local step and a transparent identity (1.0 + 0.0j)
 * Phase Correction Term for every tone, so the downstream combiner sees a
 * structurally valid ORIGIN_REFLECTOR event with a no-op phase rotation.
 *
 * @param p_local_event  Output: initiator event to populate step data into.
 * @param p_peer_event   Output: reflector event (identity PCT) to populate.
 * @param p_local_steps  Net buffer containing local step data in HCI format.
 * @param role           CS role (initiator or reflector).
 */
void cs_step_parse_inline(SubeventResultEvent_t * p_local_event,
                          SubeventResultEvent_t * p_peer_event,
                          struct net_buf_simple * p_local_steps,
                          enum bt_conn_le_cs_role role);

#endif  // CS_STEP_PARSE_H
