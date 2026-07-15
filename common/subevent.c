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

#include "cs_step_parse.h"

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
static void fill_subevent_header(SubeventResultEvent_t *                p_event,
                                 Origin_t                               origin,
                                 uint64_t                               local_mac,
                                 uint64_t                               peer_mac,
                                 struct bt_conn_le_cs_subevent_result * p_result,
                                 uint8_t                                antenna_path_count)
{
    p_event->origin                                        = origin;
    p_event->local_mac                                     = local_mac;
    p_event->peer_mac                                      = peer_mac;
    p_event->connection_handle                             = 0;
    p_event->config_id                                     = p_result->header.config_id;
    p_event->has_config_id                                 = true;
    p_event->procedure_done_status                         = p_result->header.procedure_done_status;
    p_event->procedure_abort_reason                        = p_result->header.procedure_abort_reason;
    p_event->subevent_done_status                          = p_result->header.subevent_done_status;
    p_event->subevent_abort_reason                         = p_result->header.subevent_abort_reason;
    p_event->antenna_path_count                            = antenna_path_count;
    p_event->step_count                                    = 0;
    p_event->initial_meta.start_acl_conn_event_counter     = p_result->header.start_acl_conn_event;
    p_event->initial_meta.has_start_acl_conn_event_counter = true;
    p_event->initial_meta.procedure_counter                = p_result->header.procedure_counter;
    p_event->initial_meta.frequency_compensation.value     = p_result->header.frequency_compensation;
    p_event->initial_meta.reference_power_level.value      = p_result->header.reference_power_level;
    p_event->has_initial_meta                              = true;
}

/**
 * @brief Populate initiator and reflector SubeventResultEvent structures from CS subevent data.
 *
 * Parses both local and peer step data into SubeventResultEvent structures
 * suitable for feeding into the spectral ranging flow.
 *
 * @param p_local_event   Output: populated initiator event.
 * @param p_peer_event    Output: populated reflector event.
 * @param local_mac       Local Bluetooth MAC address.
 * @param peer_mac        Peer Bluetooth MAC address.
 * @param p_result        Pointer to the local subevent result header.
 * @param p_local_steps   Net buffer containing local step data.
 * @param p_peer_steps    Net buffer containing peer step data.
 * @param role            CS role (initiator or reflector).
 */
void subevent_populate(SubeventResultEvent_t *                p_local_event,
                       SubeventResultEvent_t *                p_peer_event,
                       uint64_t                               local_mac,
                       uint64_t                               peer_mac,
                       struct bt_conn_le_cs_subevent_result * p_result,
                       struct net_buf_simple *                p_local_steps,
                       struct net_buf_simple *                p_peer_steps,
                       enum bt_conn_le_cs_role                role)
{
    for (size_t i = 0; i < 160; i++)
    {
        memset(&p_local_event->steps.idx[i], 0, sizeof(p_local_event->steps.idx[i]));
        memset(&p_peer_event->steps.idx[i], 0, sizeof(p_peer_event->steps.idx[i]));
        p_local_event->steps.idx[i].mode = MARS_CS_STEP_MODE_INVALID;
        p_peer_event->steps.idx[i].mode  = MARS_CS_STEP_MODE_INVALID;
    }


    /* RAS derives antenna_path_count from the ranging header inside cs_step_parse,
     * so seed both events with 0 here.
     */
    fill_subevent_header(p_local_event, ORIGIN_INITIATOR, local_mac, peer_mac, p_result, 0);
    fill_subevent_header(p_peer_event, ORIGIN_REFLECTOR, peer_mac, local_mac, p_result, 0);

    cs_step_parse(p_local_event, p_peer_event, p_peer_steps, p_local_steps, role);
}

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

    for (size_t i = 0; i < 160; i++)
    {
        memset(&p_local_event->steps.idx[i], 0, sizeof(p_local_event->steps.idx[i]));
        memset(&p_peer_event->steps.idx[i], 0, sizeof(p_peer_event->steps.idx[i]));
        p_local_event->steps.idx[i].mode = MARS_CS_STEP_MODE_INVALID;
        p_peer_event->steps.idx[i].mode  = MARS_CS_STEP_MODE_INVALID;
    }

    fill_subevent_header(p_local_event, ORIGIN_INITIATOR, local_mac, peer_mac, p_result, n_ap);
    fill_subevent_header(p_peer_event, ORIGIN_REFLECTOR, peer_mac, local_mac, p_result, n_ap);

    cs_step_parse_inline(p_local_event, p_peer_event, p_local_steps, role);
}
