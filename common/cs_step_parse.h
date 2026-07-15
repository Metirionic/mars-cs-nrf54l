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

/** @brief Sentinel mode value marking a step slot as carrying no valid data.
 *
 * Written by subevent_populate()/subevent_populate_inline() into every step
 * slot of the static SubeventResultEvent_t buffers before parsing, so that
 * slots beyond step_count (never overwritten by fill_mode2_step) carry an
 * explicit "invalid" marker instead of stale data from a prior procedure.
 *
 * Mirrored by mars-bluetooth-hci's step_mode::MODE_INVALID (Rust) — kept in
 * sync by convention; safer_ffi does not emit Rust pub consts to the C header.
 */
#define MARS_CS_STEP_MODE_INVALID 0xFFu

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
