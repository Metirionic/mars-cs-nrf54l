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

/**
 * @brief Callback type for CS procedure enable notifications.
 *
 * Called with the negotiated procedure-enable parameters after CS procedures are
 * enabled. Allows the caller to save the configured timing/subevent parameters
 * for later use (e.g. emitting them alongside per-procedure results).
 */
typedef void (*bt_le_cs_procedure_enable_cb)(struct bt_conn_le_cs_procedure_enable_complete * p_params);

void ble_callbacks_register(struct k_sem * p_sem_connected,
                            struct k_sem * p_sem_security,
                            struct k_sem * p_sem_remote_capabilities_obtained,
                            struct k_sem * p_sem_config_created,
                            struct k_sem * p_sem_cs_security_enabled);

void ble_callbacks_set_subevent_data_cb(bt_le_cs_subevent_data_available_cb p_cb);

void ble_callbacks_set_config_created_cb(bt_le_cs_config_created_cb p_cb);

void ble_callbacks_set_procedure_enable_cb(bt_le_cs_procedure_enable_cb p_cb);

#endif /* BLE_CALLBACKS_H */
