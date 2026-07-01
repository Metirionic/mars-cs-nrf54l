/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Shared CS step data parsing into SubeventResultEvent structures
 */

#include "cs_step_parse.h"

#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_BT_RAS_RREQ)
#include <bluetooth/services/ras.h>
#endif

/**
 * @brief Maximum tone index (four paths + extension slot).
 */
#define MAX_TONE_COUNT 5u

LOG_MODULE_DECLARE(app_main, LOG_LEVEL_INF);

/**
 * @brief Convert HCI tone quality pair to ToneQualityIndicator.
 *
 * Returns the lower (worse) of the two quality indicators. In IPT mode both
 * args are the local tone's indicator (the reflector contribution is embedded),
 * so the local value is returned unchanged.
 */
static ToneQualityIndicator_t tone_quality_from_hci(uint8_t local_quality_indicator, uint8_t peer_quality_indicator)
{
    static const ToneQualityIndicator_t hci_to_ordinal[] = {
        [BT_HCI_LE_CS_TONE_QUALITY_UNAVAILABLE] = TONE_QUALITY_INDICATOR_UNAVAILABLE,
        [BT_HCI_LE_CS_TONE_QUALITY_LOW]         = TONE_QUALITY_INDICATOR_LOW,
        [BT_HCI_LE_CS_TONE_QUALITY_MED]         = TONE_QUALITY_INDICATOR_MEDIUM,
        [BT_HCI_LE_CS_TONE_QUALITY_HIGH]        = TONE_QUALITY_INDICATOR_HIGH,
    };

    ToneQualityIndicator_t local = (local_quality_indicator < ARRAY_SIZE(hci_to_ordinal))
                                       ? hci_to_ordinal[local_quality_indicator]
                                       : TONE_QUALITY_INDICATOR_UNAVAILABLE;
    ToneQualityIndicator_t peer  = (peer_quality_indicator < ARRAY_SIZE(hci_to_ordinal))
                                       ? hci_to_ordinal[peer_quality_indicator]
                                       : TONE_QUALITY_INDICATOR_UNAVAILABLE;

    return (local < peer) ? local : peer;
}

/**
 * @brief Convert HCI extension slot pair to ExtensionSlot.
 *
 * Returns the most permissive combination of local and peer indicators. In
 * IPT mode both args are the local tone's indicator.
 */
static ExtensionSlot_t extension_slot_from_hci(uint8_t local_extension_slot, uint8_t peer_extension_slot)
{
    if (local_extension_slot == EXTENSION_SLOT_EXPECTED_PRESENT ||
        peer_extension_slot == EXTENSION_SLOT_EXPECTED_PRESENT)
    {
        return EXTENSION_SLOT_EXPECTED_PRESENT;
    }
    if (local_extension_slot == EXTENSION_SLOT_NOT_EXPECTED_PRESENT ||
        peer_extension_slot == EXTENSION_SLOT_NOT_EXPECTED_PRESENT)
    {
        return EXTENSION_SLOT_NOT_EXPECTED_PRESENT;
    }
    return EXTENSION_SLOT_NOT_PRESENT;
}

/**
 * @brief Convert Phase Correction Term (PCT) IQ values to normalized floats.
 *
 * Parses the 3-byte PCT via bt_le_cs_parse_pct() and normalizes by 2^15
 * to produce floats in [-1.0, 1.0].
 */
static void pct_to_float(const uint8_t pct[3], float * p_i_value, float * p_q_value)
{
    struct bt_le_cs_iq_sample iq            = bt_le_cs_parse_pct(pct);
    const float               NORMALIZATION = 32768.0f;
    *p_i_value                              = (float)iq.i / NORMALIZATION;
    *p_q_value                              = (float)iq.q / NORMALIZATION;
}

/**
 * @brief Fill one Mode-2 step into both the local and peer SubeventResultEvents.
 *
 * Shared by the RAS path (process_step_data, peer step data available) and the
 * IPT path (inline_process_step_mode2, peer step data is NULL). When
 * @p p_peer_step is NULL the IPT semantics apply: quality/extension are derived
 * from the local tone only (passed as both args), and the peer (reflector)
 * event is filled with a transparent identity PCT (1.0 + 0.0j) since the
 * reflector's phase contribution is already embedded in the local tones.
 *
 * Guards against truncated step data: if @p p_local_step->data_len is smaller
 * than the mode-2 header plus n_ap tone_info entries, the step is logged and
 * skipped (step_index is not advanced), preventing over-reads when a procedure
 * negotiates fewer antenna paths than the controller's compile-time max.
 *
 * @param p_ctx         Parse context (holds n_ap and the output events).
 * @param p_local_step  Local (initiator) HCI subevent step (mode-2).
 * @param p_peer_step   Peer (reflector) HCI subevent step, or NULL for IPT.
 *
 * @return true if iteration should continue, false if the step array is full
 *         (step_index >= 160) and the caller should stop walking steps.
 */
static bool fill_mode2_step(struct cs_step_parse_context *  ctx,
                            struct bt_le_cs_subevent_step * p_local_step,
                            struct bt_le_cs_subevent_step * p_peer_step)
{
    if (ctx->step_index >= 160)
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

    struct bt_hci_le_cs_step_data_mode_2 * local_step_data = (struct bt_hci_le_cs_step_data_mode_2 *)p_local_step->data;
    struct bt_hci_le_cs_step_data_mode_2 * peer_step_data =
        (p_peer_step != NULL) ? (struct bt_hci_le_cs_step_data_mode_2 *)p_peer_step->data : NULL;

    for (size_t event_index = 0; event_index < 2; event_index++)
    {
        SubeventResultEvent_t * p_event      = (event_index == 0u) ? ctx->p_local_event : ctx->p_peer_event;
        const bool              is_initiator = (event_index == 0u);
        Step_t *                p_step       = &p_event->steps.idx[ctx->step_index];

        p_step->mode      = p_local_step->mode;
        p_step->channel   = p_local_step->channel;
        p_step->info.kind = MODE_ROLE_SPECIFIC_INFO_KIND_MODE2;

        Mode2_t * p_mode2 = &p_step->info.mode2;
        memset(p_mode2, 0, sizeof(*p_mode2));
        p_mode2->antenna_permutation_index = 0u;

        for (size_t tone_index = 0u; tone_index < n_ap && tone_index < MAX_TONE_COUNT; tone_index++)
        {
            struct bt_hci_le_cs_step_data_tone_info * local_tone = &local_step_data->tone_info[tone_index];

            if (peer_step_data != NULL)
            {
                struct bt_hci_le_cs_step_data_tone_info * peer_tone = &peer_step_data->tone_info[tone_index];
                p_mode2->quality_indicators.idx[tone_index] =
                    tone_quality_from_hci(local_tone->quality_indicator, peer_tone->quality_indicator);
                p_mode2->extension_slots.idx[tone_index] =
                    extension_slot_from_hci(local_tone->extension_indicator, peer_tone->extension_indicator);
            }
            else
            {
                /* IPT: local tone only for both args. */
                p_mode2->quality_indicators.idx[tone_index] =
                    tone_quality_from_hci(local_tone->quality_indicator, local_tone->quality_indicator);
                p_mode2->extension_slots.idx[tone_index] =
                    extension_slot_from_hci(local_tone->extension_indicator, local_tone->extension_indicator);
            }

            float i_val, q_val;
            if (is_initiator || peer_step_data == NULL)
            {
                /* Initiator event always uses the local PCT. In IPT mode the
                 * reflector event also uses the local PCT's transparent identity
                 * (1.0 + 0.0j) since the reflector contribution is embedded. */
                if (peer_step_data == NULL && !is_initiator)
                {
                    i_val = 1.0f;
                    q_val = 0.0f;
                }
                else
                {
                    pct_to_float(local_tone->phase_correction_term, &i_val, &q_val);
                }
            }
            else
            {
                pct_to_float(peer_step_data->tone_info[tone_index].phase_correction_term, &i_val, &q_val);
            }

            p_mode2->phase_correction_terms.idx[tone_index].i = i_val;
            p_mode2->phase_correction_terms.idx[tone_index].q = q_val;
        }
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

    ctx->p_local_event->antenna_path_count = ctx->n_ap;
    ctx->p_peer_event->antenna_path_count  = ctx->n_ap;

    return true;
}

/** @brief Callback that converts HCI Mode-2 step data into SubeventResultEvent fields. */
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

/**
 * @brief Parse HCI step data and populate SubeventResultEvent step fields.
 *
 * Calls bt_ras_rreq_rd_subevent_data_parse with internal callbacks that
 * convert HCI Mode-2 step data into SubeventResultEvent fields.
 * Sets step_count on both events after parsing.
 *
 * @param p_local_event  Output: initiator event to populate step data into.
 * @param p_peer_event   Output: reflector event to populate step data into.
 * @param p_peer_steps   Net buffer containing peer step data.
 * @param p_local_steps  Net buffer containing local step data.
 * @param role         CS role (initiator or reflector).
 */
void cs_step_parse(SubeventResultEvent_t * p_local_event,
                   SubeventResultEvent_t * p_peer_event,
                   struct net_buf_simple * p_peer_steps,
                   struct net_buf_simple * p_local_steps,
                   enum bt_conn_le_cs_role role)
{
    struct cs_step_parse_context ctx = {
        .p_local_event = p_local_event,
        .p_peer_event  = p_peer_event,
        .step_index    = 0,
        .n_ap          = 0,
    };

    bt_ras_rreq_rd_subevent_data_parse(p_peer_steps,
                                       p_local_steps,
                                       role,
                                       process_ranging_header,
                                       NULL,
                                       process_step_data,
                                       &ctx);

    p_local_event->step_count = ctx.step_index;
    p_peer_event->step_count  = ctx.step_index;
}
#endif /* CONFIG_BT_RAS_RREQ */

/**
 * @brief Number of antenna paths reported by the controller configuration.
 *
 * Used by cs_step_parse_inline() to size the per-step tone loop in IPT mode,
 * where the RAS ranging header (which sets n_ap in the RAS path) is not parsed.
 */
static inline uint8_t inline_n_ap(void)
{
    uint8_t n_ap = CONFIG_BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS;
    if (n_ap == 0u)
    {
        n_ap = 1u;
    }
    return MIN(n_ap, MAX_TONE_COUNT);
}

/** @brief Fill one Mode-2 step into both events for the inline (IPT) path.
 *
 * Delegates to fill_mode2_step() with a NULL peer step, which applies IPT
 * semantics (local tone for quality/extension via tone_quality_from_hci /
 * extension_slot_from_hci, identity PCT for the peer event).
 */
static void inline_process_step_mode2(struct cs_step_parse_context * ctx, struct bt_le_cs_subevent_step * local_step)
{
    (void)fill_mode2_step(ctx, local_step, NULL);
}

void cs_step_parse_inline(SubeventResultEvent_t * p_local_event,
                          SubeventResultEvent_t * p_peer_event,
                          struct net_buf_simple * p_local_steps,
                          enum bt_conn_le_cs_role role)
{
    ARG_UNUSED(role);

    /* Derive n_ap from the negotiated procedure header (captured in
     * antenna_path_count by serialize_run from
     * bt_conn_le_cs_subevent_result.header.num_antenna_paths). Fall back to the
     * controller's compile-time max only if the header value was unavailable,
     * so a procedure that negotiates fewer paths than the build max does not
     * over-read tone_info past the step's real data_len.
     */
    uint8_t n_ap = p_local_event->antenna_path_count;
    if (n_ap == 0u)
    {
        n_ap                              = inline_n_ap();
        p_local_event->antenna_path_count = n_ap;
        p_peer_event->antenna_path_count  = n_ap;
    }
    n_ap = MIN(MAX(n_ap, 1u), MAX_TONE_COUNT);

    struct cs_step_parse_context ctx = {
        .p_local_event = p_local_event,
        .p_peer_event  = p_peer_event,
        .step_index    = 0,
        .n_ap          = n_ap,
    };

    /* The local buffer holds raw HCI subevent steps: mode | channel | data_len | data,
     * one byte each, concatenated for all steps in the procedure. Walk it as the
     * Nordic ipt_initiator sample does (subevent_steps_parse).
     */
    while (p_local_steps->len >= 3)
    {
        struct bt_le_cs_subevent_step local_step = {0};
        local_step.mode                          = net_buf_simple_pull_u8(p_local_steps);
        local_step.channel                       = net_buf_simple_pull_u8(p_local_steps);
        local_step.data_len                      = net_buf_simple_pull_u8(p_local_steps);

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
            inline_process_step_mode2(&ctx, &local_step);
        }

        net_buf_simple_pull(p_local_steps, local_step.data_len);
    }

    p_local_event->step_count = ctx.step_index;
    p_peer_event->step_count  = ctx.step_index;
}
