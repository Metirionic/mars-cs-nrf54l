/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Local Phase Correction Term (PCT) sample types for CSV output
 *
 * Replaces the mars-bluetooth-hci Rust-owned SubeventResultEvent_t / Step_t /
 * Mode2_t with minimal local structs carrying exactly the per-step PCT data the
 * CSV serializer emits: one channel plus the first-antenna-path (tone-0) PCT I/Q
 * for the initiator (and, in RAS mode, the paired reflector PCT).
 */

#ifndef CS_PCT_H__
#define CS_PCT_H__

#include <stdint.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/net_buf.h>

/** @brief Max Mode-2 samples captured per CS procedure.
 *
 * Matches the former mars-bluetooth-hci @c Step_160_array_t capacity and the
 * step_index cap in cs_step_parse.c. A procedure that produces more Mode-2
 * steps than this stops capturing (the walk ends); do not raise without also
 * revisiting the IPT local-step buffer sizing in cs_initiator.h.
 */
#define CS_PCT_MAX_SAMPLES 160u

/** @brief One Mode-2 step's first-antenna-path (tone-0) PCT, both sides.
 *
 * @ref refl_i / @ref refl_q carry the reflector's tone-0 PCT and are only
 * populated in RAS mode (peer step data available). In IPT mode they are left
 * zero and not emitted — the reflector's phase contribution is already embedded
 * in the local tones. I/Q are the raw int16 values from @ref bt_le_cs_parse_pct
 * cast to float (NOT normalized); the serializer computes magnitude/phase.
 */
struct cs_pct_sample
{
    uint8_t channel;  /**< Selected frequency channel for the step. */
    float   init_i;   /**< Initiator tone-0 PCT in-phase (raw int16 as float). */
    float   init_q;   /**< Initiator tone-0 PCT quadrature (raw int16 as float). */
    float   refl_i;   /**< Reflector tone-0 PCT in-phase (RAS only; 0 in IPT). */
    float   refl_q;   /**< Reflector tone-0 PCT quadrature (RAS only; 0 in IPT). */
};

/** @brief All valid Mode-2 samples captured in one CS procedure.
 *
 * Populated by @ref cs_pct_populate (RAS) or @ref cs_pct_populate_inline (IPT);
 * consumed by @ref serialize_run. @ref count is the number of valid entries in
 * @ref samples (non-Mode-2 / invalid / truncated steps are never added).
 *
 * @ref subevents_total / @ref subevents_aborted carry the actual subevent count
 * for the procedure (set by the shared subevent-result handler in cs_initiator.c
 * from the per-subevent callbacks) and are emitted in the CSV metadata block.
 */
struct cs_pct_procedure
{
    struct cs_pct_sample samples[CS_PCT_MAX_SAMPLES]; /**< Valid samples, [0..count). */
    uint8_t              count;                        /**< Number of valid samples. */
    uint16_t             procedure_counter;            /**< CS procedure counter (LOG_INF only; not emitted). */
    uint8_t              subevents_total;              /**< Total subevents in the procedure (incl. aborted). */
    uint8_t              subevents_aborted;            /**< Subevents with subevent_done_status == ABORTED. */
};

/**
 * @brief Populate a cs_pct_procedure from RAS local + peer step buffers.
 *
 * Parses both the local (initiator) and peer (reflector) HCI Mode-2 step data,
 * pairing them per step (same channel), and fills @ref proc with one
 * cs_pct_sample per valid Mode-2 step using tone 0 (first antenna path) only.
 * Non-Mode-2 / truncated steps are skipped.
 *
 * @param proc          Output: procedure to populate.
 * @param p_result      Local subevent result header (procedure_counter source).
 * @param p_local_steps Net buffer containing local step data (HCI format).
 * @param p_peer_steps  Net buffer containing peer step data (HCI format).
 * @param role          CS role (initiator or reflector).
 */
void cs_pct_populate(struct cs_pct_procedure *                proc,
                      struct bt_conn_le_cs_subevent_result * p_result,
                      struct net_buf_simple *                p_local_steps,
                      struct net_buf_simple *                p_peer_steps,
                      enum bt_conn_le_cs_role                role);

/**
 * @brief Populate a cs_pct_procedure from local-only step data (IPT mode).
 *
 * Mirrors @ref cs_pct_populate but reads only the local step buffer — in inline
 * PCT (IPT) mode there is no peer step buffer (the reflector's phase contribution
 * is embedded in the local tones), so the reflector PCT fields are left zero and
 * not emitted. The antenna path count is taken from @ref p_result header so a
 * procedure negotiating fewer paths than the controller's compile-time max does
 * not over-read tone_info past the step's real data_len.
 *
 * @param proc          Output: procedure to populate.
 * @param p_result      Local subevent result header (n_ap + procedure_counter source).
 * @param p_local_steps Net buffer containing local step data (HCI format).
 */
void cs_pct_populate_inline(struct cs_pct_procedure *                proc,
                             struct bt_conn_le_cs_subevent_result * p_result,
                             struct net_buf_simple *                p_local_steps);

#endif /* CS_PCT_H__ */