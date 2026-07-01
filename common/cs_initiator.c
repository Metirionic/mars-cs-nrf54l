/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Shared Channel Sounding initiator connection and configuration flow
 */

#include "cs_initiator.h"

#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>
#include <bluetooth/services/ras.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "addr_utils.h"
#include "ble_callbacks.h"
#include "ble_scanning.h"

LOG_MODULE_DECLARE(app_main, LOG_LEVEL_INF);

/* Semaphores */
static K_SEM_DEFINE(sem_data_ready, 0, 1);
static K_SEM_DEFINE(sem_remote_capabilities_obtained, 0, 1);
static K_SEM_DEFINE(sem_config_created, 0, 1);
static K_SEM_DEFINE(sem_cs_security_enabled, 0, 1);
static K_SEM_DEFINE(sem_connected, 0, 1);
static K_SEM_DEFINE(sem_discovery_done, 0, 1);
static K_SEM_DEFINE(sem_mtu_exchange_done, 0, 1);
static K_SEM_DEFINE(sem_security, 0, 1);
static K_SEM_DEFINE(sem_local_steps, 1, 1);
static K_SEM_DEFINE(sem_ras_features, 0, 1);

NET_BUF_SIMPLE_DEFINE_STATIC(latest_local_steps, LOCAL_PROCEDURE_MEM);
NET_BUF_SIMPLE_DEFINE_STATIC(latest_peer_steps, BT_RAS_PROCEDURE_MEM);
static int32_t  local_ranging_counter   = PROCEDURE_COUNTER_NONE;
static int32_t  dropped_ranging_counter = PROCEDURE_COUNTER_NONE;
static uint32_t ras_feature_bits;

static struct bt_conn_le_cs_subevent_result latest_subevent_header;

static uint64_t local_mac;
static uint64_t peer_mac;

static cs_initiator_ranging_data_cb       ranging_data_cb_ptr;
static cs_initiator_ranging_data_ready_cb ranging_data_ready_cb_ptr;
static cs_initiator_config_created_cb     config_created_cb_ptr;

/** @brief Get the current BLE connection reference. */
struct bt_conn * cs_initiator_get_connection(void)
{
    return ble_callbacks_get_connection();
}

/** @brief Get the RAS feature bits read from the peer. */
uint32_t cs_initiator_get_ras_feature_bits(void)
{
    return ras_feature_bits;
}

/** @brief Get the local MAC address as a 64-bit integer. */
uint64_t cs_initiator_get_local_mac(void)
{
    return local_mac;
}

/** @brief Get the peer MAC address as a 64-bit integer. */
uint64_t cs_initiator_get_peer_mac(void)
{
    return peer_mac;
}

/** @brief Access the latest local step data buffer. */
struct net_buf_simple * cs_initiator_get_local_steps(void)
{
    return &latest_local_steps;
}

/** @brief Access the latest peer step data buffer. */
struct net_buf_simple * cs_initiator_get_peer_steps(void)
{
    return &latest_peer_steps;
}

/** @brief Get the latest local ranging counter value. */
int32_t cs_initiator_get_local_ranging_counter(void)
{
    return local_ranging_counter;
}

/** @brief Access the latest subevent result header. */
struct bt_conn_le_cs_subevent_result * cs_initiator_get_latest_subevent_header(void)
{
    return &latest_subevent_header;
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

/** @brief Register the ranging data callback for realtime or on-demand RD mode. */
void cs_initiator_set_ranging_data_cb(cs_initiator_ranging_data_cb cb)
{
    ranging_data_cb_ptr = cb;
}

/** @brief Register callback for RAS "data ready" notifications (on-demand RD mode). */
void cs_initiator_set_ranging_data_ready_cb(cs_initiator_ranging_data_ready_cb cb)
{
    ranging_data_ready_cb_ptr = cb;
}

/** @brief Register callback invoked when CS config is created. */
void cs_initiator_set_config_created_cb(cs_initiator_config_created_cb cb)
{
    config_created_cb_ptr = cb;
}

/** @brief Internal forwarder for RAS "data ready" notifications (on-demand RD mode). */
static void ranging_data_ready_cb(struct bt_conn * p_conn, uint16_t ranging_counter)
{
    LOG_DBG("Ranging data ready %i", ranging_counter);

    if (ranging_data_ready_cb_ptr)
    {
        ranging_data_ready_cb_ptr(p_conn, ranging_counter);
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

/** @brief Callback for local CS subevent results — accumulates step data into net buffers. */
static void subevent_result_cb(struct bt_conn * p_conn, struct bt_conn_le_cs_subevent_result * result)
{
    LOG_INF("Got subevent result %d.", (int)result->header.procedure_counter);

    if (dropped_ranging_counter == result->header.procedure_counter)
    {
        return;
    }

    uint16_t procedure_ranging_counter = bt_ras_rreq_get_ranging_counter(result->header.procedure_counter);
    if (local_ranging_counter != procedure_ranging_counter)
    {
        local_ranging_counter = procedure_ranging_counter;
        int sem_state         = k_sem_take(&sem_local_steps, K_NO_WAIT);

        if (sem_state < 0)
        {
            net_buf_simple_reset(&latest_local_steps);
            net_buf_simple_reset(&latest_peer_steps);

            dropped_ranging_counter = result->header.procedure_counter;
            LOG_DBG("Dropped subevent results due to unfinished ranging data request.");
            return;
        }
    }

    latest_subevent_header.header        = result->header;
    latest_subevent_header.step_data_buf = NULL;

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
            return;
        }
    }

    dropped_ranging_counter = PROCEDURE_COUNTER_NONE;

    if (result->header.procedure_done_status == BT_CONN_LE_CS_PROCEDURE_COMPLETE)
    {
        local_ranging_counter = procedure_ranging_counter;
    }
    else if (result->header.procedure_done_status == BT_CONN_LE_CS_PROCEDURE_ABORTED)
    {
        LOG_WRN("Procedure %u aborted", result->header.procedure_counter);
        net_buf_simple_reset(&latest_local_steps);
        k_sem_give(&sem_local_steps);
    }
}

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
    ble_callbacks_set_config_created_cb(config_created_cb_ptr);

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
    local_mac = addr_to_u64(&local_addrs[0]);

    peer_mac = addr_to_u64((bt_addr_le_t *)bt_conn_get_dst(ble_callbacks_get_connection()));

    err = bt_conn_set_security(ble_callbacks_get_connection(), BT_SECURITY_L2);
    if (err)
    {
        LOG_ERR("Failed to encrypt connection (err %d)", err);
        return err;
    }

    k_sem_take(&sem_security, K_FOREVER);

    bt_gatt_exchange_mtu(ble_callbacks_get_connection(), &mtu_exchange_params);

    k_sem_take(&sem_mtu_exchange_done, K_FOREVER);

    err = bt_gatt_dm_start(ble_callbacks_get_connection(), BT_UUID_RANGING_SERVICE, &discovery_cb, NULL);
    if (err)
    {
        LOG_ERR("Discovery failed (err %d)", err);
        return err;
    }

    k_sem_take(&sem_discovery_done, K_FOREVER);

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
        err =
            bt_ras_rreq_realtime_rd_subscribe(ble_callbacks_get_connection(), &latest_peer_steps, ranging_data_cb_ptr);
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

    err = bt_le_cs_read_remote_supported_capabilities(ble_callbacks_get_connection());
    if (err)
    {
        LOG_ERR("Failed to exchange CS capabilities (err %d)", err);
        return err;
    }

    k_sem_take(&sem_remote_capabilities_obtained, K_FOREVER);

    struct bt_le_cs_create_config_params config_params = {
        .id                     = CS_CONFIG_ID,
        .mode                   = BT_CONN_LE_CS_MAIN_MODE_2_SUB_MODE_1,
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
    const uint8_t PREFERRED_PEER_ANTENNA = antenna_get_mask_for_role();

    LOG_INF("Local antennas: %d, paths: %d, using config: %d, preferred peer ant.: %d",
            CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS,
            CONFIG_BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS,
            ANTENNA_CONFIG,
            PREFERRED_PEER_ANTENNA);

    const struct bt_le_cs_set_procedure_parameters_param procedure_params = {
        .config_id                     = CS_CONFIG_ID,
        .max_procedure_len             = p_config->max_procedure_len,
        .min_procedure_interval        = p_config->min_procedure_interval,
        .max_procedure_interval        = p_config->max_procedure_interval,
        .max_procedure_count           = 0,
        .min_subevent_len              = p_config->min_subevent_len,
        .max_subevent_len              = p_config->max_subevent_len,
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
