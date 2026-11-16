/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Shared BLE connection and Channel Sounding callbacks
 */

#ifndef BLE_CALLBACKS_H
#define BLE_CALLBACKS_H

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/kernel.h>

/** One setup phase: callbacks store a negative errno or zero before waking the waiter. */
struct ble_setup_completion
{
    struct k_sem done;
    int          result;
};

static inline void ble_setup_complete(struct ble_setup_completion * completion, int result)
{
    completion->result = result;
    k_sem_give(&completion->done);
}

struct bt_conn * ble_callbacks_get_connection(void);

/**
 * @brief Callback type for CS subevent data available notifications.
 */
typedef void (*bt_le_cs_subevent_data_available_cb)(struct bt_conn *                       p_conn,
                                                    struct bt_conn_le_cs_subevent_result * p_result);

/**
 * @brief Callback type for CS config creation notifications.
 *
 * Called with the negotiated CS config after a successful config creation.
 * Allows the caller to save the config for later use.
 */
typedef void (*bt_le_cs_config_created_cb)(struct bt_conn_le_cs_config * p_config);

void ble_callbacks_register(struct ble_setup_completion * p_setup_connected,
                            struct ble_setup_completion * p_setup_security,
                            struct ble_setup_completion * p_setup_remote_capabilities_obtained,
                            struct ble_setup_completion * p_setup_config_created,
                            struct ble_setup_completion * p_setup_cs_security_enabled,
                            struct ble_setup_completion * p_setup_procedure_enabled);

void ble_callbacks_set_subevent_data_cb(bt_le_cs_subevent_data_available_cb p_cb);

void ble_callbacks_set_config_created_cb(bt_le_cs_config_created_cb p_cb);

#endif /* BLE_CALLBACKS_H */
