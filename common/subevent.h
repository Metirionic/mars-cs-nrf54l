/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief CS subevent data parsing into a cs_pct_procedure
 */

#ifndef SUBEVENT_H
#define SUBEVENT_H

#include "cs_pct.h"

/**
 * @brief Populate a cs_pct_procedure from CS subevent data (RAS).
 *
 * Parses both local and peer step data into the procedure's cs_pct_samples
 * (one per valid Mode-2 step, tone 0, first antenna path) for the CSV
 * serializer. Non-Mode-2 / truncated steps are skipped.
 *
 * @param proc          Output: populated procedure.
 * @param p_result      Pointer to the local subevent result header.
 * @param p_local_steps Net buffer containing local step data.
 * @param p_peer_steps  Net buffer containing peer step data.
 * @param role          CS role (initiator or reflector).
 */
void cs_pct_populate(struct cs_pct_procedure *                proc,
                     struct bt_conn_le_cs_subevent_result * p_result,
                     struct net_buf_simple *                 p_local_steps,
                     struct net_buf_simple *                 p_peer_steps,
                     enum bt_conn_le_cs_role                 role);

/**
 * @brief Populate a cs_pct_procedure from local-only CS subevent data (IPT).
 *
 * Mirrors cs_pct_populate() but reads only the local step buffer — in inline
 * PCT (IPT) mode there is no peer step buffer (the reflector's phase
 * contribution is embedded in the local tones), so the reflector PCT fields are
 * left zero. The antenna path count is taken from the per-procedure
 * bt_conn_le_cs_subevent_result.header.num_antenna_paths so a procedure that
 * negotiates fewer antenna paths than the controller's compile-time max does not
 * cause cs_pct_parse_inline to over-read tone_info past the step's real
 * data_len.
 *
 * @param proc          Output: populated procedure.
 * @param p_result      Pointer to the local subevent result header.
 * @param p_local_steps Net buffer containing local step data.
 */
void cs_pct_populate_inline(struct cs_pct_procedure *                proc,
                            struct bt_conn_le_cs_subevent_result * p_result,
                            struct net_buf_simple *                 p_local_steps);

#endif /* SUBEVENT_H */