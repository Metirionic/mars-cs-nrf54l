/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief BLE scanning and Ranging Service UUID filtering
 */

#include "ble_scanning.h"

#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>
#include <bluetooth/services/ras.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app_main, LOG_LEVEL_INF);

/** @brief Callback for scan filter match — logs the matching device address. */
static void scan_filter_match(struct bt_scan_device_info *  p_device_info,
                              struct bt_scan_filter_match * p_filter_match,
                              bool                          connectable)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(p_device_info->recv_info->addr, addr, sizeof(addr));

    LOG_INF("Filters matched. Address: %s connectable: %d", addr, connectable);
}

/** @brief Callback for scan connection error — restarts scanning. */
static void scan_connecting_error(struct bt_scan_device_info * p_device_info)
{
    int err;

    LOG_INF("Connecting failed, restarting scanning");

    err = bt_scan_start(BT_SCAN_TYPE_SCAN_PASSIVE);
    if (err)
    {
        LOG_ERR("Failed to restart scanning (err %i)", err);
        return;
    }
}

/** @brief Callback for scan connection initiated. */
static void scan_connecting(struct bt_scan_device_info * p_device_info, struct bt_conn * p_conn)
{
    LOG_INF("Connecting");
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, NULL, scan_connecting_error, scan_connecting);

/**
 * @brief Initialize BLE scanning with Ranging Service UUID filter.
 *
 * @return 0 on success, negative errno on error.
 */
int scan_init(void)
{
    int err;

    struct bt_scan_init_param param = {.scan_param = NULL,
                                       .conn_param = BT_LE_CONN_PARAM(0x10, 0x10, 0, BT_GAP_MS_TO_CONN_TIMEOUT(4000)),
                                       .connect_if_match = 1};

    bt_scan_init(&param);
    bt_scan_cb_register(&scan_cb);

    err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, BT_UUID_RANGING_SERVICE);
    if (err)
    {
        LOG_ERR("Scanning filters cannot be set (err %d)", err);
        return err;
    }

    err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
    if (err)
    {
        LOG_ERR("Filters cannot be turned on (err %d)", err);
        return err;
    }

    return 0;
}
