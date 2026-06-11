/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Channel Sounding data serializer
 */

#include "serialize.h"

#include <string.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

#include "cs_step_parse.h"

LOG_MODULE_DECLARE(app_main, LOG_LEVEL_INF);

#include "mars_bluetooth_hci.h"

/** @brief Max serialized size per event pair (160 steps x ~50 bytes + header overhead). */
#define CHUNK_SIZE 8680u

/** @brief TX buffer for COBS-encoded serialized data. Leave margin for log messages. */
static uint8_t g_serialized[CHUNK_SIZE * 2u + 1000u];
/** @brief Total bytes written to g_serialized for the current run. */
static size_t g_total_written_size = 0u;
/** @brief UART device used for COBS output. */
static const struct device * gp_cobs_uart_dev = DEVICE_DT_GET(DT_CHOSEN(cobs_uart));

/** @brief Static storage for locally recorded (initiator) event. */
static SubeventResultEvent_t g_local_event;
/** @brief Static storage for remotely recorded (reflector) event. */
static SubeventResultEvent_t g_peer_event;

/**
 * @brief Append data to the global UART TX buffer.
 *
 * @return 0 on success, -ENOMEM if the data would overflow the buffer.
 */
static int serialize_cb(void * p_data, size_t size)
{
    if (g_total_written_size + size >= sizeof(g_serialized))
    {
        LOG_ERR("Serialization buffer overflow: %u + %u >= %u", g_total_written_size, size, sizeof(g_serialized));
        return -ENOMEM;
    }
    memcpy(&g_serialized[g_total_written_size], p_data, size);
    g_total_written_size += size;
    return 0;
}

/** @brief Reset serialization state for a new run. */
static void serialize_init(void)
{
    g_total_written_size = 0u;
}

/**
 * @brief Serialize a log message and append to the global TX buffer.
 *
 * @return 0 on success, negative errno on error.
 */
static int serialize_append_log_message(char * p_message)
{
    SerializedData_t result = serialize_log_message(p_message, true);

    if (result.p_data != NULL && result.size != 0u)
    {
        int err = serialize_cb(result.p_data, result.size);
        drop_bin(result);
        return err;
    }

    drop_bin(result);
    return 0;
}

/**
 * @brief Serialize a SubeventResultEvent and append to the global TX buffer.
 *
 * @return 0 on success, negative errno on error.
 */
static int serialize_append_event(SubeventResultEvent_t * p_event)
{
    SerializedData_t result = serialize_subevent_result_event(p_event, true);

    if (result.p_data != NULL && result.size != 0u)
    {
        int err = serialize_cb(result.p_data, result.size);
        drop_bin(result);
        return err;
    }

    drop_bin(result);
    return 0;
}

/**
 * @brief Serialize local and peer CS subevent data and transmit over UART.
 *
 * Parses both local and peer step data into SubeventResultEvent structures,
 * serializes them via the Rust FFI, and transmits the COBS-encoded binary
 * over the configured UART device.
 *
 * @param local_mac  Local Bluetooth MAC address.
 * @param peer_mac   Peer Bluetooth MAC address.
 * @param p_result   Pointer to the local subevent result header.
 * @param p_local_steps  Net buffer containing local step data.
 * @param p_peer_steps   Net buffer containing peer step data.
 * @param role       CS role (initiator or reflector).
 */
void serialize_run(uint64_t                               local_mac,
                   uint64_t                               peer_mac,
                   struct bt_conn_le_cs_subevent_result * p_result,
                   struct net_buf_simple *                p_local_steps,
                   struct net_buf_simple *                p_peer_steps,
                   enum bt_conn_le_cs_role                role)
{
    serialize_init();

    LOG_INF("Run serialization for procedure %u", p_result->header.procedure_counter);

    g_local_event.origin                                        = ORIGIN_INITIATOR;
    g_local_event.local_mac                                     = local_mac;
    g_local_event.peer_mac                                      = peer_mac;
    g_local_event.connection_handle                             = 0;
    g_local_event.config_id                                     = p_result->header.config_id;
    g_local_event.has_config_id                                 = true;
    g_local_event.procedure_done_status                         = p_result->header.procedure_done_status;
    g_local_event.procedure_abort_reason                        = p_result->header.procedure_abort_reason;
    g_local_event.subevent_done_status                          = p_result->header.subevent_done_status;
    g_local_event.subevent_abort_reason                         = p_result->header.subevent_abort_reason;
    g_local_event.antenna_path_count                            = 0;
    g_local_event.step_count                                    = 0;
    g_local_event.initial_meta.start_acl_conn_event_counter     = p_result->header.start_acl_conn_event;
    g_local_event.initial_meta.has_start_acl_conn_event_counter = true;
    g_local_event.initial_meta.procedure_counter                = p_result->header.procedure_counter;
    g_local_event.initial_meta.frequency_compensation.value     = p_result->header.frequency_compensation;
    g_local_event.initial_meta.reference_power_level.value      = p_result->header.reference_power_level;
    g_local_event.has_initial_meta                              = true;

    g_peer_event.origin                                        = ORIGIN_REFLECTOR;
    g_peer_event.local_mac                                     = peer_mac;
    g_peer_event.peer_mac                                      = local_mac;
    g_peer_event.connection_handle                             = 0;
    g_peer_event.config_id                                     = p_result->header.config_id;
    g_peer_event.has_config_id                                 = true;
    g_peer_event.procedure_done_status                         = p_result->header.procedure_done_status;
    g_peer_event.procedure_abort_reason                        = p_result->header.procedure_abort_reason;
    g_peer_event.subevent_done_status                          = p_result->header.subevent_done_status;
    g_peer_event.subevent_abort_reason                         = p_result->header.subevent_abort_reason;
    g_peer_event.antenna_path_count                            = 0;
    g_peer_event.step_count                                    = 0;
    g_peer_event.initial_meta.start_acl_conn_event_counter     = p_result->header.start_acl_conn_event;
    g_peer_event.initial_meta.has_start_acl_conn_event_counter = true;
    g_peer_event.initial_meta.procedure_counter                = p_result->header.procedure_counter;
    g_peer_event.initial_meta.frequency_compensation.value     = p_result->header.frequency_compensation;
    g_peer_event.initial_meta.reference_power_level.value      = p_result->header.reference_power_level;
    g_peer_event.has_initial_meta                              = true;

    cs_step_parse(&g_local_event, &g_peer_event, p_peer_steps, p_local_steps, role);

    int err;

    err = serialize_append_event(&g_local_event);
    if (err)
    {
        LOG_ERR("Failed to serialize local event (err %d)", err);
        return;
    }

    err = serialize_append_event(&g_peer_event);
    if (err)
    {
        LOG_ERR("Failed to serialize peer event (err %d)", err);
        return;
    }

    err = serialize_append_log_message("Processing finished");
    if (err)
    {
        LOG_ERR("Failed to serialize log (err %d)", err);
        return;
    }

    err = uart_tx(gp_cobs_uart_dev, g_serialized, g_total_written_size, SYS_FOREVER_MS);

    LOG_INF("Sending %u bytes", g_total_written_size);

    if (err)
    {
        LOG_ERR("UART TX failed (err %d)", err);
    }
}
