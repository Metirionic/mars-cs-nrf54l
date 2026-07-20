/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Shared CS step data parsing into a cs_pct_procedure
 */

#include "cs_step_parse.h"

#include <zephyr/bluetooth/cs.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "cs_initiator.h"

#if defined(CONFIG_BT_RAS_RREQ)
#include <bluetooth/services/ras.h>
#endif

LOG_MODULE_DECLARE(app_main, LOG_LEVEL_INF);

/**
 * @brief Parse a 3-byte PCT to raw int16 I/Q, cast to float.
 *
 * Uses bt_le_cs_parse_pct() to extract the sign-extended int16 in-phase and
 * quadrature terms, then casts them to float. The values are left raw (not
 * normalized); the serializer computes magnitude/phase from them.
 *
 * @param pct        3-byte little-endian phase correction term.
 * @param p_i_value  Output: in-phase component as float.
 * @param p_q_value  Output: quadrature component as float.
 */
static void pct_to_float(const uint8_t pct[3], float * p_i_value, float * p_q_value)
{
    struct bt_le_cs_iq_sample iq = bt_le_cs_parse_pct(pct);

    *p_i_value = (float)iq.i;
    *p_q_value = (float)iq.q;
}

/**
 * @brief Context for the RAS/IPT step-data parse callbacks.
 */
struct cs_step_parse_context
{
    struct cs_pct_procedure * proc;       /**< Output: procedure being filled. */
    uint8_t                   step_index; /**< Current sample index within the procedure. */
    uint8_t                   n_ap;       /**< Number of antenna paths in current ranging header. */
};

/**
 * @brief Fill one Mode-2 step's cs_pct_sample (tone 0, first antenna path).
 *
 * Shared by the RAS path (process_step_data, peer step data available) and the
 * IPT path (cs_pct_parse_inline, peer step data is NULL). When @p p_peer_step is
 * NULL the reflector PCT fields are left zero (IPT embeds the reflector phase in
 * the local tones, so the reflector PCT is not emitted).
 *
 * Guards against truncated step data: if @p p_local_step->data_len is smaller
 * than the mode-2 header plus n_ap tone_info entries, the step is logged and
 * skipped (step_index is not advanced), preventing over-reads when a procedure
 * negotiates fewer antenna paths than the controller's compile-time max.
 *
 * Only tone_info[0] (the first antenna path) is read; the remaining n_ap-1
 * antenna paths and the extension slot are ignored.
 *
 * @param p_ctx         Parse context (holds n_ap and the output procedure).
 * @param p_local_step  Local (initiator) HCI subevent step (mode-2).
 * @param p_peer_step   Peer (reflector) HCI subevent step, or NULL for IPT.
 *
 * @return true if iteration should continue, false if the sample array is full
 *         (step_index >= CS_PCT_MAX_SAMPLES) and the caller should stop walking.
 */
static bool fill_mode2_step(struct cs_step_parse_context *  ctx,
                            struct bt_le_cs_subevent_step * p_local_step,
                            struct bt_le_cs_subevent_step * p_peer_step)
{
    if (ctx->step_index >= CS_PCT_MAX_SAMPLES)
    {
        return false;
    }

    const uint8_t n_ap = ctx->n_ap;
    const size_t  min_data_len =
        sizeof(struct bt_hci_le_cs_step_data_mode_2) + n_ap * sizeof(struct bt_hci_le_cs_step_data_tone_info);
    if (p_local_step->data_len < min_data_len)
    {
        LOG_WRN("Mode-2 step data truncated: data_len=%u < required=%zu (n_ap=%u)",
                (unsigned int)p_local_step->data_len,
                min_data_len,
                (unsigned int)n_ap);
        return true;
    }

    struct bt_hci_le_cs_step_data_mode_2 * local_step_data =
        (struct bt_hci_le_cs_step_data_mode_2 *)p_local_step->data;

    struct cs_pct_sample * p_sample = &ctx->proc->samples[ctx->step_index];
    p_sample->channel = p_local_step->channel;

    float i_val;
    float q_val;
    pct_to_float(local_step_data->tone_info[0].phase_correction_term, &i_val, &q_val);
    p_sample->init_i = i_val;
    p_sample->init_q = q_val;

    if (p_peer_step != NULL)
    {
        struct bt_hci_le_cs_step_data_mode_2 * peer_step_data =
            (struct bt_hci_le_cs_step_data_mode_2 *)p_peer_step->data;
        pct_to_float(peer_step_data->tone_info[0].phase_correction_term, &i_val, &q_val);
        p_sample->refl_i = i_val;
        p_sample->refl_q = q_val;
    }
    else
    {
        /* IPT: no reflector PCT (embedded in the local tones). */
        p_sample->refl_i = 0.0f;
        p_sample->refl_q = 0.0f;
    }

    ctx->step_index++;
    return true;
}

#if IS_ENABLED(CONFIG_BT_RAS_RREQ)
/** @brief Callback that extracts the antenna path count from the ranging header. */
static bool process_ranging_header(struct ras_ranging_header * p_ranging_header, void * p_user_data)
{
    struct cs_step_parse_context * ctx = (struct cs_step_parse_context *)p_user_data;

    ctx->n_ap =
        MAX(1,
            ((p_ranging_header->antenna_paths_mask & BIT(0)) + ((p_ranging_header->antenna_paths_mask & BIT(1)) >> 1) +
             ((p_ranging_header->antenna_paths_mask & BIT(2)) >> 2) +
             ((p_ranging_header->antenna_paths_mask & BIT(3)) >> 3)));

    return true;
}

/** @brief Callback that converts an HCI Mode-2 step pair into a cs_pct_sample. */
static bool process_step_data(struct bt_le_cs_subevent_step * p_local_step,
                              struct bt_le_cs_subevent_step * p_peer_step,
                              void *                          p_user_data)
{
    struct cs_step_parse_context * p_context = (struct cs_step_parse_context *)p_user_data;

    if (p_local_step->mode == BT_HCI_OP_LE_CS_MAIN_MODE_2)
    {
        return fill_mode2_step(p_context, p_local_step, p_peer_step);
    }

    return true;
}

void cs_pct_parse(struct cs_pct_procedure * proc,
                  struct net_buf_simple *   p_peer_steps,
                  struct net_buf_simple *   p_local_steps,
                  enum bt_conn_le_cs_role    role)
{
    struct cs_step_parse_context ctx = {
        .proc       = proc,
        .step_index = 0,
        .n_ap       = 0,
    };

    bt_ras_rreq_rd_subevent_data_parse(p_peer_steps,
                                       p_local_steps,
                                       role,
                                       process_ranging_header,
                                       NULL,
                                       process_step_data,
                                       &ctx);

    proc->count = ctx.step_index;
}
#endif /* CONFIG_BT_RAS_RREQ */

void cs_pct_parse_inline(struct cs_pct_procedure *                proc,
                         struct bt_conn_le_cs_subevent_result * p_result,
                         struct net_buf_simple *                 p_local_steps)
{
    /* Derive n_ap from the negotiated procedure header. Fall back to the
     * controller's compile-time max only if the header value was unavailable,
     * so a procedure that negotiates fewer paths than the build max does not
     * over-read tone_info past the step's real data_len.
     */
    uint8_t n_ap = p_result->header.num_antenna_paths;
    if (n_ap == 0u)
    {
        n_ap = MARS_CS_MAX_ANTENNA_PATHS;
    }
    n_ap = MIN(MAX(n_ap, 1u), MARS_CS_MAX_TONE_COUNT);

    struct cs_step_parse_context ctx = {
        .proc       = proc,
        .step_index = 0,
        .n_ap       = n_ap,
    };

    /* The local buffer holds raw HCI subevent steps: mode | channel | data_len | data,
     * one byte each, concatenated for all steps in the procedure. Walk it as the
     * Nordic ipt_initiator sample does (subevent_steps_parse).
     */
    while (p_local_steps->len >= MARS_CS_IPT_STEP_FRAMING_LEN)
    {
        struct bt_le_cs_subevent_step local_step = {0};
        local_step.mode                            = net_buf_simple_pull_u8(p_local_steps);
        local_step.channel                         = net_buf_simple_pull_u8(p_local_steps);
        local_step.data_len                         = net_buf_simple_pull_u8(p_local_steps);

        if (local_step.data_len == 0)
        {
            LOG_WRN("Inline parse: zero-length step data.");
            break;
        }
        if (local_step.data_len > p_local_steps->len)
        {
            LOG_WRN("Inline parse: local step data appears malformed.");
            break;
        }

        local_step.data = p_local_steps->data;

        if (local_step.mode == BT_HCI_OP_LE_CS_MAIN_MODE_2)
        {
            (void)fill_mode2_step(&ctx, &local_step, NULL);
        }

        net_buf_simple_pull(p_local_steps, local_step.data_len);
    }

    proc->count = ctx.step_index;
}