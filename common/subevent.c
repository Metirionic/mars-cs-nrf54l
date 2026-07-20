/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief CS subevent data parsing into a cs_pct_procedure
 */

#include "subevent.h"

#include <zephyr/logging/log.h>

#include "cs_step_parse.h"

LOG_MODULE_DECLARE(app_main, LOG_LEVEL_INF);

/**
 * @brief Populate a cs_pct_procedure from CS subevent data (RAS).
 *
 * Seeds the procedure's procedure_counter from the local subevent result header
 * and delegates step parsing to cs_pct_parse() (RAS), which fills one
 * cs_pct_sample per valid Mode-2 step (tone 0, first antenna path) from the
 * paired local + peer step buffers.
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
                     enum bt_conn_le_cs_role                 role)
{
    proc->count             = 0;
    proc->procedure_counter = p_result->header.procedure_counter;

    cs_pct_parse(proc, p_peer_steps, p_local_steps, role);
}

/**
 * @brief Populate a cs_pct_procedure from local-only CS subevent data (IPT).
 *
 * Seeds the procedure's procedure_counter from the local subevent result header
 * and delegates step parsing to cs_pct_parse_inline() (IPT), which reads only
 * the local step buffer. The reflector PCT fields are left zero (IPT embeds the
 * reflector phase in the local tones); n_ap is derived from the header inside
 * cs_pct_parse_inline().
 *
 * @param proc          Output: populated procedure.
 * @param p_result      Pointer to the local subevent result header.
 * @param p_local_steps Net buffer containing local step data.
 */
void cs_pct_populate_inline(struct cs_pct_procedure *                proc,
                            struct bt_conn_le_cs_subevent_result * p_result,
                            struct net_buf_simple *                 p_local_steps)
{
    proc->count             = 0;
    proc->procedure_counter = p_result->header.procedure_counter;

    cs_pct_parse_inline(proc, p_result, p_local_steps);
}