/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SERIALIZE_H__
#define SERIALIZE_H__

#include <stdint.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/net_buf.h>

#if !defined(CONFIG_MARS_CS_INLINE_PCT)
#include <bluetooth/services/ras.h>
#endif

void serialize_run(uint64_t                               local_mac,
                   uint64_t                               peer_mac,
                   struct bt_conn_le_cs_subevent_result * p_result,
                   struct net_buf_simple *                p_local_steps,
                   struct net_buf_simple *                p_peer_steps,
                   enum bt_conn_le_cs_role                role);

#endif /* SERIALIZE_H__ */
