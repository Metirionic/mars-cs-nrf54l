/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief CS subevent data parsing into SubeventResultEvent structures
 */

#include "subevent.h"

#include <string.h>
#include <zephyr/logging/log.h>

#include "cs_initiator.h"
#include "cs_step_parse.h"
#if IS_ENABLED(CONFIG_BT_RAS_RREQ)
#include <bluetooth/services/ras.h>
#endif

LOG_MODULE_DECLARE(app_main, LOG_LEVEL_INF);

/** Convert an HCI procedure abort reason to the serialization enum. */
static ProcedureAbortReason_t procedure_abort_reason_from_hci(uint8_t reason)
{
    if (reason == 0x0f)
    {
        return PROCEDURE_ABORT_REASON_UNSPECIFIED;
    }
    if (reason <= 3)
    {
        return (ProcedureAbortReason_t)reason;
    }
    return PROCEDURE_ABORT_REASON_RESERVED;
}

/** Convert an HCI subevent abort reason to the serialization enum. */
static SubeventAbortReason_t subevent_abort_reason_from_hci(uint8_t reason)
{
    if (reason == 0x0f)
    {
        return SUBEVENT_ABORT_REASON_UNSPECIFIED;
    }
    if (reason <= 3)
    {
        return (SubeventAbortReason_t)reason;
    }
    return SUBEVENT_ABORT_REASON_RESERVED;
}

/**
 * @brief Fill the common SubeventResultEvent header fields shared by both
 *        subevent_populate() (RAS) and subevent_populate_inline() (IPT).
 *
 * The two events' headers are identical except for origin/MAC placement, which
 * mirrors the event perspective (initiator vs reflector).
 *
 * @param p_event              Output event to populate.
 * @param origin               ORIGIN_INITIATOR or ORIGIN_REFLECTOR.
 * @param local_mac            MAC address of the event's "local" side.
 * @param peer_mac             MAC address of the event's "peer" side.
 * @param p_result             Local subevent result header.
 * @param antenna_path_count   Initial antenna path count (RAS path passes 0 and
 *                             derives it from the ranging header later; IPT
 *                             passes p_result->header.num_antenna_paths).
 */
static void fill_subevent_header(SubeventResultEvent_t *                      p_event,
                                 Origin_t                                     origin,
                                 uint64_t                                     local_mac,
                                 uint64_t                                     peer_mac,
                                 const struct bt_conn_le_cs_subevent_result * p_result,
                                 uint8_t                                      antenna_path_count)
{
    p_event->origin                 = origin;
    p_event->local_mac              = local_mac;
    p_event->peer_mac               = peer_mac;
    p_event->connection_handle      = 0;
    p_event->config_id              = p_result->header.config_id;
    p_event->has_config_id          = true;
    p_event->procedure_done_status  = p_result->header.procedure_done_status;
    p_event->procedure_abort_reason = procedure_abort_reason_from_hci(p_result->header.procedure_abort_reason);
    p_event->subevent_done_status   = p_result->header.subevent_done_status;
    p_event->subevent_abort_reason  = subevent_abort_reason_from_hci(p_result->header.subevent_abort_reason);
    p_event->antenna_path_count     = antenna_path_count;
    p_event->step_count             = 0;
    p_event->initial_meta.start_acl_conn_event_counter     = p_result->header.start_acl_conn_event;
    p_event->initial_meta.has_start_acl_conn_event_counter = true;
    p_event->initial_meta.procedure_counter                = p_result->header.procedure_counter;
    p_event->initial_meta.frequency_compensation.value     = p_result->header.frequency_compensation;
    p_event->initial_meta.reference_power_level.value      = p_result->header.reference_power_level;
    p_event->has_initial_meta                              = true;
}

#if IS_ENABLED(CONFIG_BT_RAS_RREQ)
struct ras_parse_context
{
    struct cs_step_parse_context     steps;
    const struct cs_local_subevent * p_subevents;
    size_t                           count;
    size_t                           index;
    uint64_t                         local_mac;
    uint64_t                         peer_mac;
    enum bt_conn_le_cs_role          role;
    subevent_emit_cb                 p_emit;
    struct net_buf_simple *          p_local_steps;
    size_t                           local_remaining;
    uint8_t                          expected_steps;
    uint8_t                          peer_step_count;
    bool                             failed;
};

static bool parse_ranging_header(struct ras_ranging_header * p_header, void * p_user_data)
{
    struct ras_parse_context * p_context = p_user_data;
    if (p_context->count == 0 ||
        p_header->ranging_counter != (p_context->p_subevents[0].result.header.procedure_counter & 0x0fff) ||
        p_header->config_id != p_context->p_subevents[0].result.header.config_id)
    {
        p_context->failed = true;
        return false;
    }
    p_context->steps.n_ap = POPCOUNT(p_header->antenna_paths_mask);
    if (p_context->steps.n_ap == 0 || p_context->steps.n_ap > MARS_CS_MAX_ANTENNA_PATHS)
    {
        p_context->failed = true;
        return false;
    }
    return true;
}

static void emit_subevent(struct ras_parse_context * p_context)
{
    p_context->steps.p_local_event->step_count = p_context->steps.step_index;
    p_context->steps.p_peer_event->step_count  = p_context->peer_step_count;
    p_context->p_emit(p_context->steps.p_local_event, p_context->steps.p_peer_event);
}

static bool parse_subevent_header(struct ras_subevent_header * p_peer, void * p_user_data)
{
    struct ras_parse_context * p_context = p_user_data;
    if (p_context->index >= p_context->count ||
        (p_context->index != 0 && p_context->peer_step_count != p_context->expected_steps))
    {
        p_context->failed = true;
        return false;
    }
    const struct cs_local_subevent * p_local = &p_context->p_subevents[p_context->index++];
    if (p_local->result.header.start_acl_conn_event != p_peer->start_acl_conn_event ||
        p_local->data_len > p_context->p_local_steps->len ||
        (p_peer->num_steps_reported != 0 && p_local->result.header.num_steps_reported != p_peer->num_steps_reported))
    {
        LOG_ERR("Local and peer subevent boundaries do not match");
        p_context->failed = true;
        return false;
    }
    memset(p_context->steps.p_local_event, 0, sizeof(*p_context->steps.p_local_event));
    memset(p_context->steps.p_peer_event, 0, sizeof(*p_context->steps.p_peer_event));
    fill_subevent_header(p_context->steps.p_local_event,
                         p_context->role == BT_CONN_LE_CS_ROLE_INITIATOR ? ORIGIN_INITIATOR : ORIGIN_REFLECTOR,
                         p_context->local_mac,
                         p_context->peer_mac,
                         &p_local->result,
                         p_context->steps.n_ap);
    struct bt_conn_le_cs_subevent_result peer_result = p_local->result;
    peer_result.header.start_acl_conn_event          = p_peer->start_acl_conn_event;
    peer_result.header.frequency_compensation        = p_peer->freq_compensation;
    peer_result.header.reference_power_level         = p_peer->ref_power_level;
    peer_result.header.procedure_done_status         = p_peer->ranging_done_status;
    peer_result.header.subevent_done_status          = p_peer->subevent_done_status;
    peer_result.header.procedure_abort_reason        = p_peer->ranging_abort_reason;
    peer_result.header.subevent_abort_reason         = p_peer->subevent_abort_reason;
    fill_subevent_header(p_context->steps.p_peer_event,
                         p_context->role == BT_CONN_LE_CS_ROLE_INITIATOR ? ORIGIN_REFLECTOR : ORIGIN_INITIATOR,
                         p_context->peer_mac,
                         p_context->local_mac,
                         &peer_result,
                         p_context->steps.n_ap);
    p_context->steps.step_index = 0;
    p_context->peer_step_count  = 0;
    p_context->expected_steps   = p_peer->num_steps_reported;
    p_context->local_remaining  = p_context->p_local_steps->len - p_local->data_len;
    if (p_context->expected_steps == 0)
    {
        /* Preserve the local measurements even when RAS omits peer data. */
        struct net_buf_simple slice;
        net_buf_simple_init_with_data(&slice, p_context->p_local_steps->data, p_local->data_len);
        while (slice.len != 0)
        {
            if (slice.len < MARS_CS_IPT_STEP_FRAMING_LEN)
            {
                p_context->failed = true;
                return false;
            }
            struct bt_le_cs_subevent_step step;
            step.mode     = net_buf_simple_pull_u8(&slice);
            step.channel  = net_buf_simple_pull_u8(&slice);
            step.data_len = net_buf_simple_pull_u8(&slice);
            step.data     = slice.data;
            if (step.data_len > slice.len || !cs_step_parse_local_step(&p_context->steps, &step))
            {
                p_context->failed = true;
                return false;
            }
            net_buf_simple_pull(&slice, step.data_len);
        }
        if (p_context->steps.step_index != p_local->result.header.num_steps_reported)
        {
            p_context->failed = true;
            return false;
        }
        net_buf_simple_pull(p_context->p_local_steps, p_local->data_len);
        emit_subevent(p_context);
    }
    return true;
}

static bool parse_step(struct bt_le_cs_subevent_step * p_local,
                       struct bt_le_cs_subevent_step * p_peer,
                       void *                          p_user_data)
{
    struct ras_parse_context * p_context = p_user_data;
    /* Nordic calls us after pulling the local three-byte step prefix. */
    if (p_context->p_local_steps->len < p_context->local_remaining ||
        p_local->data_len > p_context->p_local_steps->len - p_context->local_remaining ||
        !cs_step_parse_step(&p_context->steps, p_local, p_peer))
    {
        p_context->failed = true;
        return false;
    }
    p_context->peer_step_count = p_context->steps.step_index;
    if (p_context->peer_step_count == p_context->expected_steps)
    {
        if (p_context->p_local_steps->len - p_local->data_len != p_context->local_remaining)
        {
            p_context->failed = true;
            return false;
        }
        emit_subevent(p_context);
    }
    return true;
}

bool subevent_populate(SubeventResultEvent_t *          p_local_event,
                       SubeventResultEvent_t *          p_peer_event,
                       uint64_t                         local_mac,
                       uint64_t                         peer_mac,
                       const struct cs_local_subevent * p_subevents,
                       size_t                           count,
                       struct net_buf_simple *          p_local_steps,
                       struct net_buf_simple *          p_peer_steps,
                       enum bt_conn_le_cs_role          role,
                       subevent_emit_cb                 p_emit)
{
    if (count == 0 || p_emit == NULL || p_peer_steps->len < sizeof(struct ras_ranging_header))
    {
        return false;
    }
    struct ras_parse_context context = {
        .steps         = {.p_local_event = p_local_event, .p_peer_event = p_peer_event},
        .p_subevents   = p_subevents,
        .count         = count,
        .local_mac     = local_mac,
        .peer_mac      = peer_mac,
        .role          = role,
        .p_emit        = p_emit,
        .p_local_steps = p_local_steps,
    };
    bt_ras_rreq_rd_subevent_data_parse(p_peer_steps,
                                       p_local_steps,
                                       role,
                                       parse_ranging_header,
                                       parse_subevent_header,
                                       parse_step,
                                       &context);
    return !context.failed && context.index == count && context.peer_step_count == context.expected_steps &&
           p_local_steps->len == 0 && p_peer_steps->len == 0;
}
#endif

/**
 * @brief Populate initiator and reflector SubeventResultEvent structures from
 *        local-only CS subevent data (IPT mode).
 *
 * Mirrors subevent_populate() but reads only the local step buffer — in
 * inline PCT (IPT) mode there is no peer step buffer (the reflector's phase
 * contribution is embedded in the local tones). The peer (reflector) event
 * is filled with the same headers as the local event; cs_step_parse_inline()
 * then synthesizes per-step fields with identity PCTs.
 *
 * The antenna path count is taken from the per-procedure
 * bt_conn_le_cs_subevent_result.header.num_antenna_paths so a procedure that
 * negotiates fewer antenna paths than the controller's compile-time max does
 * not cause cs_step_parse_inline to over-read tone_info past the step's real
 * data_len.
 *
 * @param p_local_event   Output: populated initiator event.
 * @param p_peer_event    Output: populated reflector event (identity PCT).
 * @param local_mac       Local Bluetooth MAC address.
 * @param peer_mac        Peer Bluetooth MAC address.
 * @param p_result        Pointer to the local subevent result header.
 * @param p_local_steps   Net buffer containing local step data.
 * @param role            CS role (initiator or reflector).
 */
void subevent_populate_inline(SubeventResultEvent_t *                p_local_event,
                              SubeventResultEvent_t *                p_peer_event,
                              uint64_t                               local_mac,
                              uint64_t                               peer_mac,
                              struct bt_conn_le_cs_subevent_result * p_result,
                              struct net_buf_simple *                p_local_steps,
                              enum bt_conn_le_cs_role                role)
{
    const uint8_t n_ap = p_result->header.num_antenna_paths;

    fill_subevent_header(p_local_event, ORIGIN_INITIATOR, local_mac, peer_mac, p_result, n_ap);
    fill_subevent_header(p_peer_event, ORIGIN_REFLECTOR, peer_mac, local_mac, p_result, n_ap);

    cs_step_parse_inline(p_local_event, p_peer_event, p_local_steps, role);
}
