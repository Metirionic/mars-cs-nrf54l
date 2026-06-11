/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Shared CS step data parsing into SubeventResultEvent structures
 */

#ifndef CS_STEP_PARSE_H__
#define CS_STEP_PARSE_H__

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/net_buf.h>

#include "rust_ffi_types.h"

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

#endif /* CS_STEP_PARSE_H__ */
