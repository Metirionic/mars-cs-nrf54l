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
    p_event->origin                = origin;
    p_event->local_mac             = local_mac;
    p_event->peer_mac              = peer_mac;
    p_event->connection_handle     = 0;
    p_event->config_id             = p_result->header.config_id;
    p_event->has_config_id         = true;
    p_event->procedure_done_status = p_result->header.procedure_done_status;
    p_event->procedure_abort_reason =
        p_result->header.procedure_abort_reason == 0x0f
            ? PROCEDURE_ABORT_REASON_UNSPECIFIED
            : (p_result->header.procedure_abort_reason <= 3 ? p_result->header.procedure_abort_reason
                                                            : PROCEDURE_ABORT_REASON_RESERVED);
    p_event->subevent_done_status = p_result->header.subevent_done_status;
    p_event->subevent_abort_reason =
        p_result->header.subevent_abort_reason == 0x0f
            ? SUBEVENT_ABORT_REASON_UNSPECIFIED
            : (p_result->header.subevent_abort_reason <= 3 ? p_result->header.subevent_abort_reason
                                                           : SUBEVENT_ABORT_REASON_RESERVED);
    p_event->antenna_path_count                            = antenna_path_count;
    p_event->step_count                                    = 0;
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
    const struct cs_local_subevent * subevents;
    size_t                           count;
    size_t                           index;
    uint64_t                         local_mac;
    uint64_t                         peer_mac;
    enum bt_conn_le_cs_role          role;
    subevent_emit_cb                 emit;
    struct net_buf_simple *          local_steps;
    size_t                           local_remaining;
    uint8_t                          expected_steps;
    uint8_t                          peer_step_count;
    bool                             failed;
};

static bool parse_ranging_header(struct ras_ranging_header * header, void * user)
{
    struct ras_parse_context * ctx = user;
    if (ctx->count == 0 || header->ranging_counter != (ctx->subevents[0].result.header.procedure_counter & 0x0fff) ||
        header->config_id != ctx->subevents[0].result.header.config_id)
    {
        ctx->failed = true;
        return false;
    }
    ctx->steps.n_ap = POPCOUNT(header->antenna_paths_mask);
    if (ctx->steps.n_ap == 0 || ctx->steps.n_ap > MARS_CS_MAX_ANTENNA_PATHS)
    {
        ctx->failed = true;
        return false;
    }
    return true;
}

static void emit_subevent(struct ras_parse_context * ctx)
{
    ctx->steps.p_local_event->step_count = ctx->steps.step_index;
    ctx->steps.p_peer_event->step_count  = ctx->peer_step_count;
    ctx->emit(ctx->steps.p_local_event, ctx->steps.p_peer_event);
}

static bool parse_subevent_header(struct ras_subevent_header * peer, void * user)
{
    struct ras_parse_context * ctx = user;
    if (ctx->index >= ctx->count || (ctx->index != 0 && ctx->peer_step_count != ctx->expected_steps))
    {
        ctx->failed = true;
        return false;
    }
    const struct cs_local_subevent * local = &ctx->subevents[ctx->index++];
    if (local->result.header.start_acl_conn_event != peer->start_acl_conn_event ||
        local->data_len > ctx->local_steps->len ||
        (peer->num_steps_reported != 0 && local->result.header.num_steps_reported != peer->num_steps_reported))
    {
        LOG_ERR("Local and peer subevent boundaries do not match");
        ctx->failed = true;
        return false;
    }
    memset(ctx->steps.p_local_event, 0, sizeof(*ctx->steps.p_local_event));
    memset(ctx->steps.p_peer_event, 0, sizeof(*ctx->steps.p_peer_event));
    fill_subevent_header(ctx->steps.p_local_event,
                         ctx->role == BT_CONN_LE_CS_ROLE_INITIATOR ? ORIGIN_INITIATOR : ORIGIN_REFLECTOR,
                         ctx->local_mac,
                         ctx->peer_mac,
                         &local->result,
                         ctx->steps.n_ap);
    struct bt_conn_le_cs_subevent_result peer_result = local->result;
    peer_result.header.start_acl_conn_event          = peer->start_acl_conn_event;
    peer_result.header.frequency_compensation        = peer->freq_compensation;
    peer_result.header.reference_power_level         = peer->ref_power_level;
    peer_result.header.procedure_done_status         = peer->ranging_done_status;
    peer_result.header.subevent_done_status          = peer->subevent_done_status;
    peer_result.header.procedure_abort_reason        = peer->ranging_abort_reason;
    peer_result.header.subevent_abort_reason         = peer->subevent_abort_reason;
    fill_subevent_header(ctx->steps.p_peer_event,
                         ctx->role == BT_CONN_LE_CS_ROLE_INITIATOR ? ORIGIN_REFLECTOR : ORIGIN_INITIATOR,
                         ctx->peer_mac,
                         ctx->local_mac,
                         &peer_result,
                         ctx->steps.n_ap);
    ctx->steps.step_index = 0;
    ctx->peer_step_count  = 0;
    ctx->expected_steps   = peer->num_steps_reported;
    ctx->local_remaining  = ctx->local_steps->len - local->data_len;
    if (ctx->expected_steps == 0)
    {
        /* Preserve the local measurements even when RAS omits peer data. */
        struct net_buf_simple slice;
        net_buf_simple_init_with_data(&slice, ctx->local_steps->data, local->data_len);
        while (slice.len != 0)
        {
            if (slice.len < MARS_CS_IPT_STEP_FRAMING_LEN)
            {
                ctx->failed = true;
                return false;
            }
            struct bt_le_cs_subevent_step step;
            step.mode     = net_buf_simple_pull_u8(&slice);
            step.channel  = net_buf_simple_pull_u8(&slice);
            step.data_len = net_buf_simple_pull_u8(&slice);
            step.data     = slice.data;
            if (step.data_len > slice.len || !cs_step_parse_local_step(&ctx->steps, &step))
            {
                ctx->failed = true;
                return false;
            }
            net_buf_simple_pull(&slice, step.data_len);
        }
        if (ctx->steps.step_index != local->result.header.num_steps_reported)
        {
            ctx->failed = true;
            return false;
        }
        net_buf_simple_pull(ctx->local_steps, local->data_len);
        emit_subevent(ctx);
    }
    return true;
}

static bool parse_step(struct bt_le_cs_subevent_step * local, struct bt_le_cs_subevent_step * peer, void * user)
{
    struct ras_parse_context * ctx = user;
    /* Nordic calls us after pulling the local three-byte step prefix. */
    if (ctx->local_steps->len < ctx->local_remaining ||
        local->data_len > ctx->local_steps->len - ctx->local_remaining || !cs_step_parse_step(&ctx->steps, local, peer))
    {
        ctx->failed = true;
        return false;
    }
    ctx->peer_step_count = ctx->steps.step_index;
    if (ctx->peer_step_count == ctx->expected_steps)
    {
        if (ctx->local_steps->len - local->data_len != ctx->local_remaining)
        {
            ctx->failed = true;
            return false;
        }
        emit_subevent(ctx);
    }
    return true;
}

bool subevent_populate(SubeventResultEvent_t *          local,
                       SubeventResultEvent_t *          peer,
                       uint64_t                         local_mac,
                       uint64_t                         peer_mac,
                       const struct cs_local_subevent * subevents,
                       size_t                           count,
                       struct net_buf_simple *          local_steps,
                       struct net_buf_simple *          peer_steps,
                       enum bt_conn_le_cs_role          role,
                       subevent_emit_cb                 emit)
{
    if (count == 0 || emit == NULL || peer_steps->len < sizeof(struct ras_ranging_header))
    {
        return false;
    }
    struct ras_parse_context ctx = {
        .steps       = {.p_local_event = local, .p_peer_event = peer},
        .subevents   = subevents,
        .count       = count,
        .local_mac   = local_mac,
        .peer_mac    = peer_mac,
        .role        = role,
        .emit        = emit,
        .local_steps = local_steps,
    };
    bt_ras_rreq_rd_subevent_data_parse(peer_steps,
                                       local_steps,
                                       role,
                                       parse_ranging_header,
                                       parse_subevent_header,
                                       parse_step,
                                       &ctx);
    return !ctx.failed && ctx.index == count && ctx.peer_step_count == ctx.expected_steps && local_steps->len == 0 &&
           peer_steps->len == 0;
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
