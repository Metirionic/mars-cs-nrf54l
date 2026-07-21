/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Channel Sounding initiator with ranging requester sample
 */

#include <zephyr/bluetooth/cs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if !defined(CONFIG_MARS_CS_INLINE_PCT)
#include <bluetooth/services/ras.h>
#endif

#include "addr_utils.h"
#include "antenna.h"
#include "cs_initiator.h"
#include "serialize.h"
#include "subevent.h"

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

/** @brief Saved CS config from config creation callback (used for role in serialization). */
static struct bt_conn_le_cs_config cs_config;

#if IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)
/**
 * @brief Process a populated cs_pct_procedure by serializing it as CSV over UART.
 *
 * Called from the shared IPT skeleton in cs_initiator.c after the local step
 * data has been parsed into a cs_pct_procedure via cs_pct_populate_inline().
 */
static void process_subevent_cb(struct cs_pct_procedure * proc)
{
    serialize_run(proc);
}
#else
/**
 * @brief Callback for ranging data received from peer (realtime or on-demand).
 *
 * Checks counter match, validates step data, populates a cs_pct_procedure via
 * cs_pct_populate, serializes as CSV over UART, and signals the main loop.
 */
static void ranging_data_cb(struct bt_conn * p_conn, uint16_t ranging_counter, int err)
{
    ARG_UNUSED(p_conn);

    if (err)
    {
        LOG_ERR("Error when receiving ranging data with ranging counter %d (err %d)", ranging_counter, err);
        return;
    }

    int32_t local_ranging_counter = cs_initiator_get_local_ranging_counter();

    if (ranging_counter != local_ranging_counter)
    {
        LOG_INF(
            "Ranging data dropped as peer ranging counter doesn't match local ranging "
            "data counter. (peer: %u, local: %u)",
            ranging_counter,
            local_ranging_counter);
        net_buf_simple_reset(cs_initiator_get_local_steps());

        if (!(cs_initiator_get_ras_feature_bits() & RAS_FEAT_REALTIME_RD))
        {
            net_buf_simple_reset(cs_initiator_get_peer_steps());
        }

        cs_initiator_give_sem_local_steps();
        return;
    }

    LOG_DBG("Ranging data received for ranging counter %d", ranging_counter);

    struct net_buf_simple * latest_local_steps = cs_initiator_get_local_steps();

    if (latest_local_steps->len == 0)
    {
        LOG_WRN("All subevents in ranging counter %u were aborted", local_ranging_counter);
        net_buf_simple_reset(latest_local_steps);
        cs_initiator_give_sem_local_steps();

        if (!(cs_initiator_get_ras_feature_bits() & RAS_FEAT_REALTIME_RD))
        {
            net_buf_simple_reset(cs_initiator_get_peer_steps());
        }
        return;
    }

    static struct cs_pct_procedure proc;

    cs_pct_populate(&proc,
                    cs_initiator_get_latest_subevent_header(),
                    latest_local_steps,
                    cs_initiator_get_peer_steps(),
                    cs_config.role);

    cs_initiator_get_subevent_counts(&proc.subevents_total, &proc.subevents_aborted);

    serialize_run(&proc);

    net_buf_simple_reset(latest_local_steps);

    if (!(cs_initiator_get_ras_feature_bits() & RAS_FEAT_REALTIME_RD))
    {
        net_buf_simple_reset(cs_initiator_get_peer_steps());
    }

    cs_initiator_give_sem_local_steps();
    cs_initiator_give_sem_data_ready();
}

/** @brief Callback for RAS "data ready" — requests on-demand ranging data from peer. */
static void ranging_data_ready_cb(struct bt_conn * p_conn, uint16_t ranging_counter)
{
    LOG_DBG("Ranging data ready %i", ranging_counter);

    if (ranging_counter == cs_initiator_get_local_ranging_counter())
    {
        int err = bt_ras_rreq_cp_get_ranging_data(cs_initiator_get_connection(),
                                                  cs_initiator_get_peer_steps(),
                                                  ranging_counter,
                                                  ranging_data_cb);
        if (err)
        {
            LOG_ERR("Get ranging data failed (err %d)", err);
            net_buf_simple_reset(cs_initiator_get_local_steps());
            net_buf_simple_reset(cs_initiator_get_peer_steps());
            cs_initiator_give_sem_local_steps();
        }
    }
}
#endif /* CONFIG_MARS_CS_INLINE_PCT */

/** @brief Hook that saves the negotiated CS config for later use. */
static void config_create_hook(struct bt_conn_le_cs_config * config)
{
    cs_config = *config;
}

int main(void)
{
    int err;

    LOG_INF("Starting Mars Channel Sounding Initiator");

    err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return 0;
    }

#if IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)
    cs_initiator_set_process_subevent_cb(process_subevent_cb);
#else
    cs_initiator_set_ranging_data_cb(ranging_data_cb);
    cs_initiator_set_ranging_data_ready_cb(ranging_data_ready_cb);
#endif
    cs_initiator_set_config_created_cb(config_create_hook);

    const struct cs_initiator_config config = {
        /* Procedure cadence applies to ALL builds (RAS + IPT): this is
         * intentional, not an IPT leak — the new cadence speeds up
         * measurements across every antenna configuration (see #41).
         * It supersedes the prior RAS-only defaults (procedure_phy=1M,
         * min/max_procedure_interval=20/50, max_procedure_len=1000); any
         * RAS fixture/calibration/range baseline assuming those must be
         * re-baselined. Values match the Nordic ras_initiator sample, except
         * subevent_len is raised to 50000 (from the sample's 16000) so all 72
         * steps fit in a single subevent (at 16000 the controller splits into
         * 2 subevents): procedure_interval=10, subevent=50000,
         * max_procedure_len = acl_interval_units * (interval - 1) = 0x10 * 9 = 144.
         */
        .cs_sync_phy            = BT_CONN_LE_CS_SYNC_1M_PHY,
        .procedure_phy          = BT_LE_CS_PROCEDURE_PHY_2M,
        .min_procedure_interval = 10,
        .max_procedure_interval = 10,
        .min_subevent_len       = 50000,
        .max_subevent_len       = 50000,
        .max_procedure_len      = 144,
    };

    err = cs_initiator_start(&config);
    if (err)
    {
        LOG_ERR("CS initiator start failed (err %d)", err);
        return 0;
    }

    while (true)
    {
        cs_initiator_take_sem_data_ready();
    }

    return 0;
}
