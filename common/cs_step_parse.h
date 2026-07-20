/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Shared CS step data parsing into a cs_pct_procedure
 *
 * Replaces the former mars-bluetooth-hci SubeventResultEvent population with a
 * minimal local record (see cs_pct.h): one cs_pct_sample per valid Mode-2 step,
 * carrying the step's channel and the first-antenna-path (tone-0) PCT I/Q.
 */

#ifndef CS_STEP_PARSE_H
#define CS_STEP_PARSE_H

#include "cs_pct.h"

/**
 * @brief Parse RAS local + peer HCI Mode-2 step data into a cs_pct_procedure.
 *
 * Walks the paired local (initiator) and peer (reflector) step buffers via the
 * RAS parser, filling one cs_pct_sample per valid Mode-2 step (tone 0, first
 * antenna path). Non-Mode-2 steps are skipped; truncated local steps are skipped
 * (the data_len guard in fill_mode2_step). Sets proc->count.
 *
 * @param proc          Output: procedure whose samples[0..count) are filled.
 * @param p_peer_steps  Net buffer containing peer (reflector) step data.
 * @param p_local_steps Net buffer containing local (initiator) step data.
 * @param role          CS role (initiator or reflector).
 */
void cs_pct_parse(struct cs_pct_procedure * proc,
                  struct net_buf_simple *   p_peer_steps,
                  struct net_buf_simple *   p_local_steps,
                  enum bt_conn_le_cs_role    role);

/**
 * @brief Parse local-only HCI Mode-2 step data into a cs_pct_procedure (IPT).
 *
 * Walks the local step buffer (mode | channel | data_len | data per step),
 * filling one cs_pct_sample per valid Mode-2 step using tone 0. The reflector
 * PCT fields are left zero (IPT embeds the reflector phase in the local tones).
 * The antenna path count is derived from @p p_result so a procedure negotiating
 * fewer paths than the controller's compile-time max does not over-read
 * tone_info past the step's real data_len. Sets proc->count.
 *
 * @param proc          Output: procedure whose samples[0..count) are filled.
 * @param p_result      Local subevent result header (n_ap source).
 * @param p_local_steps Net buffer containing local step data in HCI format.
 */
void cs_pct_parse_inline(struct cs_pct_procedure *                proc,
                         struct bt_conn_le_cs_subevent_result * p_result,
                         struct net_buf_simple *                 p_local_steps);

#endif  /* CS_STEP_PARSE_H */