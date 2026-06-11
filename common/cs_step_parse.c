/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Shared CS step data parsing into SubeventResultEvent structures
 */

#include "cs_step_parse.h"

#include <bluetooth/services/ras.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(app_main, LOG_LEVEL_INF);

/**
 * @brief Convert HCI tone quality pair to ToneQualityIndicator.
 *
 * Returns the lower (worse) of the two quality indicators.
 */
static ToneQualityIndicator_t tone_quality_from_hci(uint8_t local_qi, uint8_t peer_qi)
{
    static const ToneQualityIndicator_t hci_to_ordinal[] = {
        [BT_HCI_LE_CS_TONE_QUALITY_UNAVAILABLE] = TONE_QUALITY_INDICATOR_UNAVAILABLE,
        [BT_HCI_LE_CS_TONE_QUALITY_LOW]         = TONE_QUALITY_INDICATOR_LOW,
        [BT_HCI_LE_CS_TONE_QUALITY_MED]         = TONE_QUALITY_INDICATOR_MEDIUM,
        [BT_HCI_LE_CS_TONE_QUALITY_HIGH]        = TONE_QUALITY_INDICATOR_HIGH,
    };

    ToneQualityIndicator_t local =
        (local_qi < ARRAY_SIZE(hci_to_ordinal)) ? hci_to_ordinal[local_qi] : TONE_QUALITY_INDICATOR_UNAVAILABLE;
    ToneQualityIndicator_t peer =
        (peer_qi < ARRAY_SIZE(hci_to_ordinal)) ? hci_to_ordinal[peer_qi] : TONE_QUALITY_INDICATOR_UNAVAILABLE;

    return (local < peer) ? local : peer;
}

/**
 * @brief Convert HCI extension slot pair to ExtensionSlot.
 *
 * Returns the most permissive combination of local and peer indicators.
 */
static ExtensionSlot_t extension_slot_from_hci(uint8_t local_ext, uint8_t peer_ext)
{
    if (local_ext == EXTENSION_SLOT_EXPECTED_PRESENT || peer_ext == EXTENSION_SLOT_EXPECTED_PRESENT)
    {
        return EXTENSION_SLOT_EXPECTED_PRESENT;
    }
    if (local_ext == EXTENSION_SLOT_NOT_EXPECTED_PRESENT || peer_ext == EXTENSION_SLOT_NOT_EXPECTED_PRESENT)
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
    struct cs_step_parse_context * ctx = (struct cs_step_parse_context *)p_user_data;

    if (ctx->step_index >= 160)
    {
        return false;
    }

    if (p_local_step->mode == BT_HCI_OP_LE_CS_MAIN_MODE_2)
    {
        struct bt_hci_le_cs_step_data_mode_2 * local_step_data =
            (struct bt_hci_le_cs_step_data_mode_2 *)p_local_step->data;
        struct bt_hci_le_cs_step_data_mode_2 * peer_step_data =
            (struct bt_hci_le_cs_step_data_mode_2 *)p_peer_step->data;

        for (int event_index = 0; event_index < 2; event_index++)
        {
            SubeventResultEvent_t * event  = (event_index == 0) ? ctx->p_local_event : ctx->p_peer_event;
            Origin_t                origin = (event_index == 0) ? ORIGIN_INITIATOR : ORIGIN_REFLECTOR;
            Step_t *                step   = &event->steps.idx[ctx->step_index];

            step->mode      = p_local_step->mode;
            step->channel   = p_local_step->channel;
            step->info.kind = MODE_ROLE_SPECIFIC_INFO_KIND_MODE2;

            Mode2_t * mode2 = &step->info.mode2;
            memset(mode2, 0, sizeof(*mode2));

            mode2->antenna_permutation_index = 0;

            for (uint8_t tone_index = 0; tone_index < ctx->n_ap && tone_index < 5; tone_index++)
            {
                struct bt_hci_le_cs_step_data_tone_info * local_tone = &local_step_data->tone_info[tone_index];
                struct bt_hci_le_cs_step_data_tone_info * peer_tone  = &peer_step_data->tone_info[tone_index];

                mode2->quality_indicators.idx[tone_index] =
                    tone_quality_from_hci(local_tone->quality_indicator, peer_tone->quality_indicator);
                mode2->extension_slots.idx[tone_index] =
                    extension_slot_from_hci(local_tone->extension_indicator, peer_tone->extension_indicator);

                float i_val, q_val;
                if (origin == ORIGIN_INITIATOR)
                {
                    pct_to_float(local_tone->phase_correction_term, &i_val, &q_val);
                }
                else
                {
                    pct_to_float(peer_tone->phase_correction_term, &i_val, &q_val);
                }

                mode2->phase_correction_terms.idx[tone_index].i = i_val;
                mode2->phase_correction_terms.idx[tone_index].q = q_val;
            }
        }

        ctx->step_index++;
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
