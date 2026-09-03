/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Shared Channel Sounding initiator connection and configuration flow
 */

#include "cs_initiator.h"

#include <bluetooth/scan.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "addr_utils.h"
#include "ble_callbacks.h"
#include "ble_scanning.h"
#include "subevent.h"
#if defined(CONFIG_BT_RAS_RREQ)
#include <bluetooth/gatt_dm.h>
#include <bluetooth/services/ras.h>
#endif

LOG_MODULE_DECLARE(app_main, LOG_LEVEL_INF);

/* Semaphores */
static K_SEM_DEFINE(sem_data_ready, 0, 1);
static K_SEM_DEFINE(sem_remote_capabilities_obtained, 0, 1);
static K_SEM_DEFINE(sem_config_created, 0, 1);
static K_SEM_DEFINE(sem_cs_security_enabled, 0, 1);
static K_SEM_DEFINE(sem_connected, 0, 1);
static K_SEM_DEFINE(sem_security, 0, 1);
static K_SEM_DEFINE(sem_local_steps, 1, 1);
#if IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)
/** @brief IPT event handoff (#173): 1 = the event pair below is free for the
 * RX thread to populate; 0 = the main loop owns it (serializing). */
static K_SEM_DEFINE(sem_event_free, 1, 1);
/** @brief Completed procedures dropped because the serialize consumer was busy. */
static uint32_t g_event_backlog_drops;
/** @brief Static event pair handed from the RX thread to the main loop. */
static SubeventResultEvent_t g_local_event;
static SubeventResultEvent_t g_peer_event;
#endif
#if IS_ENABLED(CONFIG_BT_RAS_RREQ)
static K_SEM_DEFINE(sem_discovery_done, 0, 1);
static K_SEM_DEFINE(sem_mtu_exchange_done, 0, 1);
static K_SEM_DEFINE(sem_ras_features, 0, 1);
NET_BUF_SIMPLE_DEFINE_STATIC(latest_peer_steps, BT_RAS_PROCEDURE_MEM);
#endif
NET_BUF_SIMPLE_DEFINE_STATIC(latest_local_steps, LOCAL_PROCEDURE_MEM);
static int32_t local_ranging_counter   = PROCEDURE_COUNTER_NONE;
static int32_t dropped_ranging_counter = PROCEDURE_COUNTER_NONE;
#if IS_ENABLED(CONFIG_BT_RAS_RREQ)
static uint32_t ras_feature_bits;
#endif

static struct bt_conn_le_cs_subevent_result g_latest_subevent_header;

static uint64_t g_local_mac;
static uint64_t g_peer_mac;

static cs_initiator_ranging_data_cb       gp_ranging_data_cb;
static cs_initiator_ranging_data_ready_cb gp_ranging_data_ready_cb;
static cs_initiator_config_created_cb     gp_config_created_cb;
static cs_initiator_process_subevent_cb   gp_process_subevent_cb;

/* Dynamic timing constants (see cs_initiator_start): the step duration is
 * computed from the negotiated config timings (the SDC reports the minimums
 * it supports) plus a margin; the connection interval is then sized to fit
 * the CS event plus the ACL event and a margin. T_SW is the SDC's fixed
 * antenna-switching time. */
#define CS_T_SW_US 10U

/** @brief Per-step timing margin for a single antenna path (us).
 *
 * A mode-2 step performs one tone measurement per antenna path (P = N_AP
 * phase measurement periods), so a step's real duration grows with the path
 * count: ~133 us measured for a single-path step (Nordic DevZone reports)
 * and 290 us on a 4-path A2_B2 link. CS_STEP_MARGIN_BASE_US plus
 * CS_STEP_MARGIN_PER_PATH_US per extra path lands the 4-path budget on the
 * measured 290 us (within ±10 us, conservatively over). Underestimation is
 * the dangerous direction: an under-budgeted subevent makes the SDC truncate
 * steps silently. */
#define CS_STEP_MARGIN_BASE_US 80U
/** @brief Additional per-step margin (us) per antenna path beyond the first. */
#define CS_STEP_MARGIN_PER_PATH_US 23U
/** @brief Upper clamp for the path-scaled step margin (us). */
#define CS_STEP_MARGIN_MAX_US 160U
#define CS_ACL_EVENT_US       7500U
#define CS_INTERVAL_MARGIN_US 5000U
/** @brief Max CS steps a single subevent may carry (Bluetooth Core spec limit). */
#define CS_MAX_STEPS_PER_SUBEVENT 160U
/** @brief Extra margin on the SDC's CS event scheduling window. */
#define CS_EVENT_MARGIN_US 3000U

/** @brief Negotiated CS config (timings) captured from the config-created event. */
static struct bt_conn_le_cs_config g_created_config;

/** @brief Get the current BLE connection reference. */
struct bt_conn * cs_initiator_get_connection(void)
{
    return ble_callbacks_get_connection();
}

/** @brief Get the RAS feature bits read from the peer. */
uint32_t cs_initiator_get_ras_feature_bits(void)
{
#if IS_ENABLED(CONFIG_BT_RAS_RREQ)
    return ras_feature_bits;
#else
    return 0;
#endif
}

/** @brief Get the local MAC address as a 64-bit integer. */
uint64_t cs_initiator_get_local_mac(void)
{
    return g_local_mac;
}

/** @brief Get the peer MAC address as a 64-bit integer. */
uint64_t cs_initiator_get_peer_mac(void)
{
    return g_peer_mac;
}

/** @brief Access the latest local step data buffer. */
struct net_buf_simple * cs_initiator_get_local_steps(void)
{
    return &latest_local_steps;
}

#if IS_ENABLED(CONFIG_BT_RAS_RREQ)
/** @brief Access the latest peer step data buffer. */
struct net_buf_simple * cs_initiator_get_peer_steps(void)
{
    return &latest_peer_steps;
}
#endif

/** @brief Get the latest local ranging counter value. */
int32_t cs_initiator_get_local_ranging_counter(void)
{
    return local_ranging_counter;
}

/** @brief Access the latest subevent result header. */
struct bt_conn_le_cs_subevent_result * cs_initiator_get_latest_subevent_header(void)
{
    return &g_latest_subevent_header;
}

/** @brief Give the sem_local_steps semaphore (signal that local data buffer is available). */
void cs_initiator_give_sem_local_steps(void)
{
    k_sem_give(&sem_local_steps);
}

/** @brief Give the sem_data_ready semaphore (signal that ranging data is ready for processing). */
void cs_initiator_give_sem_data_ready(void)
{
    k_sem_give(&sem_data_ready);
}

/** @brief Wait on the sem_data_ready semaphore (blocks until ranging data is available). */
void cs_initiator_take_sem_data_ready(void)
{
    k_sem_take(&sem_data_ready, K_FOREVER);
}

#if IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)
/** @brief Consume the pending IPT event pair on the caller's thread.
 *
 * Blocks until the RX thread has populated a completed procedure's events
 * (sem_data_ready), runs the app's process callback — the serialize + UART TX
 * path, ~289 ms per 26.6 KB event at 921600 — then releases the handoff
 * (sem_event_free) so the RX thread may populate the next procedure.
 *
 * This moves the blocking serialize path OFF the BT RX thread. Running it
 * inline in the RX thread (the pre-#173 behaviour) starved the SDC's
 * step-data delivery above ~3.5 procedures/s and the controller aborted the
 * surplus subevents — the COMPLETE-but-empty procedures of #173. While the
 * consumer is busy, the RX thread drops surplus completed procedures at the
 * handoff gate instead (counted, LOG_WRN every 128th drop).
 */
void cs_initiator_consume_pending_event(void)
{
    k_sem_take(&sem_data_ready, K_FOREVER);

    if (gp_process_subevent_cb)
    {
        gp_process_subevent_cb(&g_local_event, &g_peer_event);
    }

    k_sem_give(&sem_event_free);
}
#endif  // IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)

/** @brief Register the ranging data callback for realtime or on-demand RD mode. */
void cs_initiator_set_ranging_data_cb(cs_initiator_ranging_data_cb p_cb)
{
    gp_ranging_data_cb = p_cb;
}

/** @brief Register callback for RAS "data ready" notifications (on-demand RD mode). */
void cs_initiator_set_ranging_data_ready_cb(cs_initiator_ranging_data_ready_cb p_cb)
{
    gp_ranging_data_ready_cb = p_cb;
}

/** @brief Register callback invoked when CS config is created. */
void cs_initiator_set_config_created_cb(cs_initiator_config_created_cb p_cb)
{
    gp_config_created_cb = p_cb;
}

/** @brief Captures the negotiated config timings, then forwards to the app hook. */
static void config_created_forwarder(struct bt_conn_le_cs_config * config)
{
    g_created_config = *config;

    if (gp_config_created_cb)
    {
        gp_config_created_cb(config);
    }
}

/** @brief Register callback invoked to process populated subevent events (IPT mode). */
void cs_initiator_set_process_subevent_cb(cs_initiator_process_subevent_cb p_cb)
{
    gp_process_subevent_cb = p_cb;
}

#if IS_ENABLED(CONFIG_BT_RAS_RREQ)
/** @brief Internal forwarder for RAS "data ready" notifications (on-demand RD mode). */
static void ranging_data_ready_cb(struct bt_conn * p_conn, uint16_t ranging_counter)
{
    LOG_DBG("Ranging data ready %i", ranging_counter);

    if (gp_ranging_data_ready_cb)
    {
        gp_ranging_data_ready_cb(p_conn, ranging_counter);
    }
}

/** @brief Internal handler for RAS "data overwritten" notifications. */
static void ranging_data_overwritten_cb(struct bt_conn * p_conn, uint16_t ranging_counter)
{
    LOG_INF("Ranging data overwritten %i", ranging_counter);
}

/** @brief Callback for when RAS feature bits are read from the peer. */
static void ras_features_read_cb(struct bt_conn * p_conn, uint32_t feature_bits, int err)
{
    if (err)
    {
        LOG_WRN("Error while reading RAS feature bits (err %d)", err);
    }
    else
    {
        LOG_INF("Read RAS feature bits: 0x%x", feature_bits);
        ras_feature_bits = feature_bits;
    }

    k_sem_give(&sem_ras_features);
}
#endif  // CONFIG_BT_RAS_RREQ

/** @brief Callback for local CS subevent results — accumulates step data into net buffers. */
static void subevent_result_cb(struct bt_conn * p_conn, struct bt_conn_le_cs_subevent_result * result)
{
    LOG_INF("Got subevent result %d.", (int)result->header.procedure_counter);

    if (dropped_ranging_counter == result->header.procedure_counter)
    {
        return;
    }

#if IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)
    /* IPT mode: no RAS ranging-counter indirection; use the procedure counter directly. */
    uint16_t procedure_ranging_counter = result->header.procedure_counter;
#else   // IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)
    uint16_t procedure_ranging_counter = bt_ras_rreq_get_ranging_counter(result->header.procedure_counter);
#endif  // IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)
    if (local_ranging_counter != procedure_ranging_counter)
    {
        local_ranging_counter = procedure_ranging_counter;
        int sem_state         = k_sem_take(&sem_local_steps, K_NO_WAIT);

        if (sem_state < 0)
        {
            net_buf_simple_reset(&latest_local_steps);
#if IS_ENABLED(CONFIG_BT_RAS_RREQ)
            net_buf_simple_reset(&latest_peer_steps);
#endif  // IS_ENABLED(CONFIG_BT_RAS_RREQ)

            dropped_ranging_counter = result->header.procedure_counter;
            LOG_DBG("Dropped subevent results due to unfinished ranging data request.");
            return;
        }
    }

    g_latest_subevent_header.header        = result->header;
    g_latest_subevent_header.step_data_buf = NULL;

    if (result->header.subevent_done_status == BT_CONN_LE_CS_SUBEVENT_ABORTED)
    {
        /* The steps from this subevent will not be used. */
    }
    else if (result->step_data_buf)
    {
        if (result->step_data_buf->len <= net_buf_simple_tailroom(&latest_local_steps))
        {
            uint16_t  len       = result->step_data_buf->len;
            uint8_t * step_data = net_buf_simple_pull_mem(result->step_data_buf, len);

            net_buf_simple_add_mem(&latest_local_steps, step_data, len);
        }
        else
        {
            LOG_ERR("Not enough memory to store step data. (%d > %d)",
                    latest_local_steps.len + result->step_data_buf->len,
                    latest_local_steps.size);
            net_buf_simple_reset(&latest_local_steps);
            dropped_ranging_counter = result->header.procedure_counter;
            /* Return the sem_local_steps token taken at the new-procedure gate
             * above; IPT has no ranging_data_cb to recover it, so omitting this
             * give permanently stalls every subsequent procedure. */
            k_sem_give(&sem_local_steps);
            return;
        }
    }

    dropped_ranging_counter = PROCEDURE_COUNTER_NONE;

    if (result->header.procedure_done_status == BT_CONN_LE_CS_PROCEDURE_COMPLETE)
    {
        local_ranging_counter = procedure_ranging_counter;

#if IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)
        /* IPT: local steps are complete. The shared skeleton handles the
         * err/empty/populate/handoff protocol:
         *   - empty procedure → recover sem_local_steps (nothing to consume)
         *   - consumer busy → drop the procedure, counted (sem_event_free held)
         *   - otherwise → populate → release buffer → hand off to the main
         *     loop thread, which runs the process callback (serialize + UART)
         *     via cs_initiator_consume_pending(). The process callback must
         *     NOT run here: it blocks on the UART TX-complete handshake, and
         *     a blocked RX thread starves the SDC's step-data delivery until
         *     it aborts the surplus subevents (#173). */
        if (latest_local_steps.len == 0)
        {
            LOG_WRN("IPT procedure produced no step data");
            net_buf_simple_reset(&latest_local_steps);
            cs_initiator_give_sem_local_steps();
        }
        else if (k_sem_take(&sem_event_free, K_NO_WAIT) < 0)
        {
            g_event_backlog_drops++;
            if (g_event_backlog_drops == 1U || (g_event_backlog_drops % 128U) == 0U)
            {
                LOG_WRN("Serialize backlog: dropped %u procedures (UART slower than cadence)",
                        g_event_backlog_drops);
            }
            net_buf_simple_reset(&latest_local_steps);
            cs_initiator_give_sem_local_steps();
        }
        else
        {
            subevent_populate_inline(&g_local_event,
                                     &g_peer_event,
                                     g_local_mac,
                                     g_peer_mac,
                                     &g_latest_subevent_header,
                                     &latest_local_steps,
                                     BT_CONN_LE_CS_ROLE_INITIATOR);

            net_buf_simple_reset(&latest_local_steps);
            cs_initiator_give_sem_local_steps();
            cs_initiator_give_sem_data_ready();
        }
#endif  // IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)
    }
    else if (result->header.procedure_done_status == BT_CONN_LE_CS_PROCEDURE_ABORTED)
    {
        LOG_WRN("Procedure %u aborted", result->header.procedure_counter);
        net_buf_simple_reset(&latest_local_steps);
        k_sem_give(&sem_local_steps);

        /* No process callback on abort — mirrors the RAS err path: do not
         * wake the consumer on a failed/aborted procedure so the main loop
         * does not process stale event data.
         */
    }
}

#if IS_ENABLED(CONFIG_BT_RAS_RREQ)
/** @brief Callback for MTU exchange completion. */
static void mtu_exchange_cb(struct bt_conn * p_conn, uint8_t err, struct bt_gatt_exchange_params * params)
{
    if (err)
    {
        LOG_ERR("MTU exchange failed (err %d)", err);
        return;
    }

    LOG_INF("MTU exchange success (%u)", bt_gatt_get_mtu(p_conn));
    k_sem_give(&sem_mtu_exchange_done);
}

/** @brief Callback for GATT discovery completion — allocates RAS handles. */
static void discovery_completed_cb(struct bt_gatt_dm * p_dm, void * p_context)
{
    int err;

    LOG_INF("The discovery procedure succeeded");

    struct bt_conn * p_conn = bt_gatt_dm_conn_get(p_dm);

    bt_gatt_dm_data_print(p_dm);

    err = bt_ras_rreq_alloc_and_assign_handles(p_dm, p_conn);
    if (err)
    {
        LOG_ERR("RAS RREQ alloc init failed (err %d)", err);
    }

    err = bt_gatt_dm_data_release(p_dm);
    if (err)
    {
        LOG_ERR("Could not release the discovery data (err %d)", err);
    }

    k_sem_give(&sem_discovery_done);
}

/** @brief Callback for GATT service not found during discovery. */
static void discovery_service_not_found_cb(struct bt_conn * p_conn, void * p_context)
{
    LOG_INF("The service could not be found during the discovery, disconnecting");
    bt_conn_disconnect(ble_callbacks_get_connection(), BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

/** @brief Callback for GATT discovery error. */
static void discovery_error_found_cb(struct bt_conn * p_conn, int err, void * p_context)
{
    LOG_INF("The discovery procedure failed (err %d)", err);
    bt_conn_disconnect(ble_callbacks_get_connection(), BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

static struct bt_gatt_dm_cb discovery_cb = {
    .completed         = discovery_completed_cb,
    .service_not_found = discovery_service_not_found_cb,
    .error_found       = discovery_error_found_cb,
};

static struct bt_gatt_exchange_params mtu_exchange_params = {.func = mtu_exchange_cb};
#endif  // IS_ENABLED(CONFIG_BT_RAS_RREQ)

/**
 * @brief Execute the full CS initiator connection and configuration flow.
 *
 * Blocks until CS procedures are enabled. Performs:
 *   - BLE scan and connection
 *   - Security and MTU negotiation
 *   - GATT discovery of Ranging Service
 *   - RAS feature negotiation and subscription
 *   - CS capability exchange
 *   - CS config creation
 *   - CS security enable
 *   - CS procedure parameter configuration and enable
 *
 * @param p_config  Target-specific configuration parameters.
 * @return 0 on success, negative errno on error.
 */
int cs_initiator_start(const struct cs_initiator_config * p_config)
{
    int err;

    ble_callbacks_register(&sem_connected,
                           &sem_security,
                           &sem_remote_capabilities_obtained,
                           &sem_config_created,
                           &sem_cs_security_enabled);
    ble_callbacks_set_subevent_data_cb(subevent_result_cb);
    ble_callbacks_set_config_created_cb(config_created_forwarder);

    err = scan_init();
    if (err)
    {
        LOG_ERR("Scan init failed (err %d)", err);
        return err;
    }

    err = bt_scan_start(BT_SCAN_TYPE_SCAN_PASSIVE);
    if (err)
    {
        LOG_ERR("Scanning failed to start (err %i)", err);
        return err;
    }

    k_sem_take(&sem_connected, K_FOREVER);

    bt_addr_le_t local_addrs[1];
    size_t       local_count = 1;
    bt_id_get(local_addrs, &local_count);
    g_local_mac = addr_to_u64(&local_addrs[0]);

    g_peer_mac = addr_to_u64((bt_addr_le_t *)bt_conn_get_dst(ble_callbacks_get_connection()));

    err = bt_conn_set_security(ble_callbacks_get_connection(), BT_SECURITY_L2);
    if (err)
    {
        LOG_ERR("Failed to encrypt connection (err %d)", err);
        return err;
    }

    k_sem_take(&sem_security, K_FOREVER);

#if !IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)
    /* RAS mode: exchange MTU and discover the Ranging Service before subscribing. */
    bt_gatt_exchange_mtu(ble_callbacks_get_connection(), &mtu_exchange_params);

    k_sem_take(&sem_mtu_exchange_done, K_FOREVER);

    err = bt_gatt_dm_start(ble_callbacks_get_connection(), BT_UUID_RANGING_SERVICE, &discovery_cb, NULL);
    if (err)
    {
        LOG_ERR("Discovery failed (err %d)", err);
        return err;
    }

    k_sem_take(&sem_discovery_done, K_FOREVER);
#endif  // !IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)

    const struct bt_le_cs_set_default_settings_param default_settings = {
        .enable_initiator_role     = true,
        .enable_reflector_role     = false,
        .cs_sync_antenna_selection = BT_LE_CS_ANTENNA_SELECTION_OPT_REPETITIVE,
        .max_tx_power              = BT_HCI_OP_LE_CS_MAX_MAX_TX_POWER,
    };

    err = bt_le_cs_set_default_settings(ble_callbacks_get_connection(), &default_settings);
    if (err)
    {
        LOG_ERR("Failed to configure default CS settings (err %d)", err);
        return err;
    }

#if !IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)
    /* RAS mode: negotiate RAS feature bits and subscribe to ranging data. */
    err = bt_ras_rreq_read_features(ble_callbacks_get_connection(), ras_features_read_cb);
    if (err)
    {
        LOG_ERR("Could not get RAS features from peer (err %d)", err);
        return err;
    }

    k_sem_take(&sem_ras_features, K_FOREVER);

    const bool realtime_rd = ras_feature_bits & RAS_FEAT_REALTIME_RD;

    if (realtime_rd)
    {
        err = bt_ras_rreq_realtime_rd_subscribe(ble_callbacks_get_connection(), &latest_peer_steps, gp_ranging_data_cb);
        if (err)
        {
            LOG_ERR("RAS RREQ Real-time ranging data subscribe failed (err %d)", err);
            return err;
        }
    }
    else
    {
        err = bt_ras_rreq_rd_overwritten_subscribe(ble_callbacks_get_connection(), ranging_data_overwritten_cb);
        if (err)
        {
            LOG_ERR("RAS RREQ ranging data overwritten subscribe failed (err %d)", err);
            return err;
        }

        err = bt_ras_rreq_rd_ready_subscribe(ble_callbacks_get_connection(), ranging_data_ready_cb);
        if (err)
        {
            LOG_ERR("RAS RREQ ranging data ready subscribe failed (err %d)", err);
            return err;
        }

        err = bt_ras_rreq_on_demand_rd_subscribe(ble_callbacks_get_connection());
        if (err)
        {
            LOG_ERR("RAS RREQ On-demand ranging data subscribe failed (err %d)", err);
            return err;
        }

        err = bt_ras_rreq_cp_subscribe(ble_callbacks_get_connection());
        if (err)
        {
            LOG_ERR("RAS RREQ CP subscribe failed (err %d)", err);
            return err;
        }
    }
#endif  // !IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)

    err = bt_le_cs_read_remote_supported_capabilities(ble_callbacks_get_connection());
    if (err)
    {
        LOG_ERR("Failed to exchange CS capabilities (err %d)", err);
        return err;
    }

    k_sem_take(&sem_remote_capabilities_obtained, K_FOREVER);

    struct bt_le_cs_create_config_params config_params = {
        .id                     = CS_CONFIG_ID,
        .mode                   = BT_CONN_LE_CS_MAIN_MODE_2_NO_SUB_MODE,
        .min_main_mode_steps    = 2,
        .max_main_mode_steps    = 5,
        .main_mode_repetition   = 0,
        .mode_0_steps           = NUM_MODE_0_STEPS,
        .role                   = BT_CONN_LE_CS_ROLE_INITIATOR,
        .rtt_type               = BT_CONN_LE_CS_RTT_TYPE_AA_ONLY,
        .cs_sync_phy            = p_config->cs_sync_phy,
        .channel_map_repetition = 1,
        .channel_selection_type = BT_CONN_LE_CS_CHSEL_TYPE_3B,
        .ch3c_shape             = BT_CONN_LE_CS_CH3C_SHAPE_HAT,
        .ch3c_jump              = 2,
#if IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)
        /* Enable inline PCT transfer. */
        .cs_enhancements_1 = 1,
#endif  // IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)
    };

    bt_le_cs_set_valid_chmap_bits(config_params.channel_map);

    err = bt_le_cs_create_config(ble_callbacks_get_connection(),
                                 &config_params,
                                 BT_LE_CS_CREATE_CONFIG_CONTEXT_LOCAL_AND_REMOTE);
    if (err)
    {
        LOG_ERR("Failed to create CS config (err %d)", err);
        return err;
    }

    k_sem_take(&sem_config_created, K_FOREVER);

    err = bt_le_cs_security_enable(ble_callbacks_get_connection());
    if (err)
    {
        LOG_ERR("Failed to start CS Security (err %d)", err);
        return err;
    }

    k_sem_take(&sem_cs_security_enabled, K_FOREVER);

    const uint8_t ANTENNA_CONFIG         = antenna_get_config_for_role(BT_CONN_LE_CS_ROLE_INITIATOR);
    const uint8_t PREFERRED_PEER_ANTENNA = antenna_get_peer_mask();

    LOG_INF("Local antennas: %d, paths: %d, using config: %d, preferred peer ant.: %d",
            CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS,
            CONFIG_BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS,
            ANTENNA_CONFIG,
            PREFERRED_PEER_ANTENNA);

    /* Dynamic timing: compute the step duration from the negotiated config
     * timings (the SDC reports the minimums it supports), then size the
     * subevent and the connection interval:
     *
     * - Every mode-2 step carries one tone measurement per antenna path
     *   (P = N_AP phase measurement periods per step; the step's HCI data
     *   holds N_AP PCT slots with per-path quality indicators, all reported
     *   High on a 4-path link), so covering every (channel, path) pair takes
     *   exactly `channels` main-mode steps. The per-step time grows with the
     *   path count, hence the path-scaled margin (CS_STEP_MARGIN_*).
     * - A subevent may carry at most CS_MAX_STEPS_PER_SUBEVENT steps (spec
     *   cap). The procedure needs channels x channel_map_repetition
     *   main-mode steps, plus mode-0 steps. With repetition 1 a 72-channel
     *   map is 75 steps — always one subevent. channel_map_repetition only
     *   re-sweeps the map for redundancy; it is kept at 1 because the SDC
     *   never grows a procedure beyond a single subevent anyway (verified
     *   empirically: it truncates steps silently past the subevent budget,
     *   even with an enlarged CS event window via the vendor CS_PARAMS_SET
     *   command), so surplus steps would just be lost.
     * - The connection interval must hold the subevent plus the ACL event
     *   and a margin. */
    const uint8_t  antenna_paths    = CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS * __builtin_popcount(antenna_get_peer_mask());
    const uint32_t step_margin_us   = CLAMP(CS_STEP_MARGIN_BASE_US + (antenna_paths - 1U) * CS_STEP_MARGIN_PER_PATH_US,
                                            CS_STEP_MARGIN_BASE_US,
                                            CS_STEP_MARGIN_MAX_US);
    const uint32_t step_duration_us = g_created_config.t_pm_time_us + g_created_config.t_ip1_time_us +
                                      g_created_config.t_fcs_time_us + g_created_config.t_ip2_time_us +
                                      g_created_config.t_pm_time_us + CS_T_SW_US + step_margin_us;

    uint32_t channels = 0;
    for (size_t i = 0; i < sizeof(g_created_config.channel_map); i++)
    {
        channels += __builtin_popcount(g_created_config.channel_map[i]);
    }

    const uint32_t main_steps          = channels * g_created_config.channel_map_repetition;
    const uint32_t steps_per_subevent  = MIN(main_steps + g_created_config.mode_0_steps, CS_MAX_STEPS_PER_SUBEVENT);
    const uint32_t subevent_len_us     = steps_per_subevent * step_duration_us;
    const uint32_t cs_event_len_us     = subevent_len_us + CS_EVENT_MARGIN_US;
    const uint32_t min_interval_us     = cs_event_len_us + CS_ACL_EVENT_US + CS_INTERVAL_MARGIN_US;
    const uint16_t min_interval_units  = (uint16_t)DIV_ROUND_UP(min_interval_us, 1250U);
    const uint16_t max_procedure_units = (uint16_t)MIN(DIV_ROUND_UP(cs_event_len_us, 625U), UINT16_MAX);

    LOG_INF("Dynamic timing: step %u us, %u paths, %u steps, subevent %u us, interval %u us (%u units)",
            step_duration_us,
            antenna_paths,
            steps_per_subevent,
            subevent_len_us,
            min_interval_us,
            min_interval_units);

    if (main_steps + g_created_config.mode_0_steps > CS_MAX_STEPS_PER_SUBEVENT)
    {
        LOG_WRN(
            "Procedure needs %u steps but a subevent caps at %u on this SDC (no multi-subevent "
            "procedures); surplus steps are truncated",
            main_steps + g_created_config.mode_0_steps,
            CS_MAX_STEPS_PER_SUBEVENT);
    }

    const struct bt_le_cs_set_procedure_parameters_param procedure_params = {
        .config_id                     = CS_CONFIG_ID,
        .max_procedure_len             = max_procedure_units,
        .min_procedure_interval        = p_config->min_procedure_interval,
        .max_procedure_interval        = p_config->max_procedure_interval,
        .max_procedure_count           = 0,
        .min_subevent_len              = subevent_len_us,
        .max_subevent_len              = subevent_len_us,
        .tone_antenna_config_selection = ANTENNA_CONFIG,
        .phy                           = p_config->procedure_phy,
        .tx_power_delta                = 0x80,
        .preferred_peer_antenna        = PREFERRED_PEER_ANTENNA,
        .snr_control_initiator         = BT_LE_CS_SNR_CONTROL_NOT_USED,
        .snr_control_reflector         = BT_LE_CS_SNR_CONTROL_NOT_USED,
    };

    err = bt_le_cs_set_procedure_parameters(ble_callbacks_get_connection(), &procedure_params);
    if (err)
    {
        LOG_ERR("Failed to set procedure parameters (err %d)", err);
        return err;
    }

    /* Shrink the connection interval to the computed minimum. The central
     * sends the HCI LE Connection Update, which the peer's controller
     * applies directly; the subevent already fits both the current and the
     * target interval, so procedures can start before the update lands. */
    const struct bt_le_conn_param conn_param =
        BT_LE_CONN_PARAM_INIT(min_interval_units,
                              min_interval_units,
                              0,
                              BT_GAP_MS_TO_CONN_TIMEOUT(MARS_CONN_SUPERVISION_TIMEOUT_MS));
    err = bt_conn_le_param_update(ble_callbacks_get_connection(), &conn_param);
    if (err)
    {
        LOG_WRN("Conn param update to %u ms failed (err %d)", min_interval_units * 1250U / 1000U, err);
    }

    struct bt_le_cs_procedure_enable_param params = {
        .config_id = CS_CONFIG_ID,
        .enable    = 1,
    };

    err = bt_le_cs_procedure_enable(ble_callbacks_get_connection(), &params);
    if (err)
    {
        LOG_ERR("Failed to enable CS procedures (err %d)", err);
        return err;
    }

    return 0;
}
