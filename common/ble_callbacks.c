/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Shared BLE connection and Channel Sounding callbacks
 */

#include "ble_callbacks.h"

#include <dk_buttons_and_leds.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>

#define CON_STATUS_LED DK_LED1

LOG_MODULE_DECLARE(app_main, LOG_LEVEL_INF);

static struct bt_conn * connection;

static struct k_sem * sem_connected_ptr;
static struct k_sem * sem_security_ptr;
static struct k_sem * sem_remote_capabilities_obtained_ptr;
static struct k_sem * sem_config_created_ptr;
static struct k_sem * sem_cs_security_enabled_ptr;

static bt_le_cs_subevent_data_available_cb subevent_data_cb_ptr;
static bt_le_cs_config_created_cb          gp_config_created_cb;

/**
 * @brief Get the current BLE connection reference.
 */
struct bt_conn * ble_callbacks_get_connection(void)
{
    return connection;
}

/**
 * @brief Register semaphore pointers used by the BLE callbacks.
 *
 * @param p_sem_connected                      Semaphore given on BLE connection.
 * @param p_sem_security                       Semaphore given on security change.
 * @param p_sem_remote_capabilities_obtained   Semaphore given on CS capability exchange.
 * @param p_sem_config_created                 Semaphore given on CS config creation.
 * @param p_sem_cs_security_enabled            Semaphore given on CS security enable.
 */
void ble_callbacks_register(struct k_sem * p_sem_connected,
                            struct k_sem * p_sem_security,
                            struct k_sem * p_sem_remote_capabilities_obtained,
                            struct k_sem * p_sem_config_created,
                            struct k_sem * p_sem_cs_security_enabled)
{
    sem_connected_ptr                    = p_sem_connected;
    sem_security_ptr                     = p_sem_security;
    sem_remote_capabilities_obtained_ptr = p_sem_remote_capabilities_obtained;
    sem_config_created_ptr               = p_sem_config_created;
    sem_cs_security_enabled_ptr          = p_sem_cs_security_enabled;
}

/**
 * @brief Set the callback for CS subevent data available notifications.
 *
 * @param p_cb  Callback function pointer.
 */
void ble_callbacks_set_subevent_data_cb(bt_le_cs_subevent_data_available_cb p_cb)
{
    subevent_data_cb_ptr = p_cb;
}

/**
 * @brief Set the callback for CS config creation notifications.
 *
 * @param p_cb  Callback function pointer.
 */
void ble_callbacks_set_config_created_cb(bt_le_cs_config_created_cb p_cb)
{
    gp_config_created_cb = p_cb;
}

/**
 * @brief Internal forwarder for CS subevent data available notifications.
 */
static void subevent_data_available_cb(struct bt_conn * p_conn, struct bt_conn_le_cs_subevent_result * p_result)
{
    if (subevent_data_cb_ptr)
    {
        subevent_data_cb_ptr(p_conn, p_result);
    }
}

/** @brief Callback for BLE connection established. */
static void connected_cb(struct bt_conn * p_conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    (void)bt_addr_le_to_str(bt_conn_get_dst(p_conn), addr, sizeof(addr));
    LOG_INF("Connected to %s (err 0x%02X)", addr, err);

    if (err)
    {
        bt_conn_unref(p_conn);
        connection = NULL;
    }
    else
    {
        connection = bt_conn_ref(p_conn);

        k_sem_give(sem_connected_ptr);

        dk_set_led_on(CON_STATUS_LED);
    }
}

/** @brief Callback for BLE disconnection — reboots the device. */
static void disconnected_cb(struct bt_conn * p_conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason 0x%02X)", reason);

    bt_conn_unref(p_conn);
    connection = NULL;

    dk_set_led_off(CON_STATUS_LED);

    sys_reboot(SYS_REBOOT_COLD);
}

/** @brief Callback for CS remote capabilities exchange completion. */
static void remote_capabilities_cb(struct bt_conn * p_conn, uint8_t status, struct bt_conn_le_cs_capabilities * params)
{
    ARG_UNUSED(p_conn);
    ARG_UNUSED(params);

    if (status == BT_HCI_ERR_SUCCESS)
    {
        LOG_INF("CS capability exchange completed.");
        k_sem_give(sem_remote_capabilities_obtained_ptr);
    }
    else
    {
        LOG_WRN("CS capability exchange failed. (HCI status 0x%02x)", status);
    }
}

/** @brief Callback for CS config creation — logs config details and signals semaphore. */
static void config_create_cb(struct bt_conn * p_conn, uint8_t status, struct bt_conn_le_cs_config * config)
{
    ARG_UNUSED(p_conn);

    if (status == BT_HCI_ERR_SUCCESS)
    {
        const char * mode_str[5]       = {"Unused", "1 (RTT)", "2 (PBR)", "3 (RTT + PBR)", "Invalid"};
        const char * role_str[3]       = {"Initiator", "Reflector", "Invalid"};
        const char * rtt_type_str[8]   = {"AA only",
                                          "32-bit sounding",
                                          "96-bit sounding",
                                          "32-bit random",
                                          "64-bit random",
                                          "96-bit random",
                                          "128-bit random",
                                          "Invalid"};
        const char * phy_str[4]        = {"Invalid", "LE 1M PHY", "LE 2M PHY", "LE 2M 2BT PHY"};
        const char * chsel_type_str[3] = {"Algorithm #3b", "Algorithm #3c", "Invalid"};
        const char * ch3c_shape_str[3] = {"Hat shape", "X shape", "Invalid"};

        uint8_t mode_idx       = config->mode > 0 && config->mode < 4 ? config->mode : 4;
        uint8_t role_idx       = MIN(config->role, 2);
        uint8_t rtt_type_idx   = MIN(config->rtt_type, 7);
        uint8_t phy_idx        = config->cs_sync_phy > 0 && config->cs_sync_phy < 4 ? config->cs_sync_phy : 0;
        uint8_t chsel_type_idx = MIN(config->channel_selection_type, 2);
        uint8_t ch3c_shape_idx = MIN(config->ch3c_shape, 2);

        LOG_INF(
            "CS config creation complete.\n"
            " - id: %u\n"
            " - mode: %s\n"
            " - min_main_mode_steps: %u\n"
            " - max_main_mode_steps: %u\n"
            " - main_mode_repetition: %u\n"
            " - mode_0_steps: %u\n"
            " - role: %s\n"
            " - rtt_type: %s\n"
            " - cs_sync_phy: %s\n"
            " - channel_map_repetition: %u\n"
            " - channel_selection_type: %s\n"
            " - ch3c_shape: %s\n"
            " - ch3c_jump: %u\n"
            " - t_ip1_time_us: %u\n"
            " - t_ip2_time_us: %u\n"
            " - t_fcs_time_us: %u\n"
            " - t_pm_time_us: %u\n"
            " - channel_map: 0x%08X%08X%04X\n",
            config->id,
            mode_str[mode_idx],
            config->min_main_mode_steps,
            config->max_main_mode_steps,
            config->main_mode_repetition,
            config->mode_0_steps,
            role_str[role_idx],
            rtt_type_str[rtt_type_idx],
            phy_str[phy_idx],
            config->channel_map_repetition,
            chsel_type_str[chsel_type_idx],
            ch3c_shape_str[ch3c_shape_idx],
            config->ch3c_jump,
            config->t_ip1_time_us,
            config->t_ip2_time_us,
            config->t_fcs_time_us,
            config->t_pm_time_us,
            sys_get_le32(&config->channel_map[6]),
            sys_get_le32(&config->channel_map[2]),
            sys_get_le16(&config->channel_map[0]));

        if (gp_config_created_cb)
        {
            gp_config_created_cb(config);
        }

        k_sem_give(sem_config_created_ptr);
    }
    else
    {
        LOG_WRN("CS config creation failed. (HCI status 0x%02x)", status);
    }
}

/** @brief Callback for CS security enable completion. */
static void security_enable_cb(struct bt_conn * p_conn, uint8_t status)
{
    ARG_UNUSED(p_conn);

    if (status == BT_HCI_ERR_SUCCESS)
    {
        LOG_INF("CS security enabled.");
        k_sem_give(sem_cs_security_enabled_ptr);
    }
    else
    {
        LOG_WRN("CS security enable failed. (HCI status 0x%02x)", status);
    }
}

/** @brief Callback for CS procedure enable completion — logs procedure parameters. */
static void procedure_enable_cb(struct bt_conn *                                 p_conn,
                                uint8_t                                          status,
                                struct bt_conn_le_cs_procedure_enable_complete * params)
{
    ARG_UNUSED(p_conn);

    if (status == BT_HCI_ERR_SUCCESS)
    {
        if (params->state == 1)
        {
            LOG_INF(
                "CS procedures enabled:\n"
                " - config ID: %u\n"
                " - antenna configuration index: %u\n"
                " - TX power: %d dbm\n"
                " - subevent length: %u us\n"
                " - subevents per event: %u\n"
                " - subevent interval: %u\n"
                " - event interval: %u\n"
                " - procedure interval: %u\n"
                " - procedure count: %u\n"
                " - maximum procedure length: %u",
                params->config_id,
                params->tone_antenna_config_selection,
                params->selected_tx_power,
                params->subevent_len,
                params->subevents_per_event,
                params->subevent_interval,
                params->event_interval,
                params->procedure_interval,
                params->procedure_count,
                params->max_procedure_len);
        }
        else
        {
            LOG_INF("CS procedures disabled.");
        }
    }
    else
    {
        LOG_WRN("CS procedures enable failed. (HCI status 0x%02x)", status);
    }
}

/** @brief Callback for LE connection parameter request — rejects all updates. */
static bool le_param_req(struct bt_conn * p_conn, struct bt_le_conn_param * param)
{
    return false;
}

/** @brief Callback for security level change — signals semaphore on success. */
static void security_changed(struct bt_conn * p_conn, bt_security_t level, enum bt_security_err err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(p_conn), addr, sizeof(addr));

    if (err)
    {
        LOG_ERR("Security failed: %s level %u err %d %s", addr, level, err, bt_security_err_to_str(err));
        return;
    }

    LOG_INF("Security changed: %s level %u", addr, level);
    k_sem_give(sem_security_ptr);
}

BT_CONN_CB_DEFINE(conn_cb) = {
    .connected                               = connected_cb,
    .disconnected                            = disconnected_cb,
    .le_param_req                            = le_param_req,
    .security_changed                        = security_changed,
    .le_cs_read_remote_capabilities_complete = remote_capabilities_cb,
    .le_cs_config_complete                   = config_create_cb,
    .le_cs_security_enable_complete          = security_enable_cb,
    .le_cs_procedure_enable_complete         = procedure_enable_cb,
    .le_cs_subevent_data_available           = subevent_data_available_cb,
};
