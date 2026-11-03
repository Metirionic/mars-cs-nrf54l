/* SPDX-License-Identifier: MIT */
#ifndef RAS_WORKER_H
#define RAS_WORKER_H

#include "cs_initiator.h"

struct cs_local_subevent;

/** Copy a complete RAS procedure into worker-owned storage without waiting.
 * Returns false on invalid input or backlog; never retains producer pointers.
 */
bool ras_worker_submit(const struct cs_local_subevent * subevents,
                       size_t                           count,
                       const struct net_buf_simple *    local_steps,
                       const struct net_buf_simple *    peer_steps,
                       uint64_t                         local_mac,
                       uint64_t                         peer_mac,
                       enum bt_conn_le_cs_role          role);

/** Queue a diagnostic without blocking the Bluetooth RX thread. */
void ras_worker_log(const char * message);

/** Wait for and process one work item on the main thread (including UART waits). */
void ras_worker_process(void);

#endif
