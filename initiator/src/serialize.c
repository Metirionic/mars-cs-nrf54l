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
 * @brief Fill the common SubeventResultEvent header fields shared by both
 *        events serialized in serialize_run() (RAS and IPT).
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
 *                             derives it from the ranging header inside
 *                             cs_step_parse; IPT passes
 *                             p_result->header.num_antenna_paths so a procedure
 *                             that negotiates fewer antenna paths than the
 *                             controller's compile-time max does not cause
 *                             cs_step_parse_inline to over-read tone_info past
 *                             the step's real data_len).
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

#if IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)
    /* IPT: antenna path count from the per-procedure negotiated header so a
     * procedure that negotiates fewer antenna paths than the controller's
     * compile-time max does not cause cs_step_parse_inline to over-read
     * tone_info past the step's real data_len.
     */
    const uint8_t n_ap = p_result->header.num_antenna_paths;
#else
    /* RAS derives antenna_path_count from the ranging header inside cs_step_parse. */
    const uint8_t n_ap = 0;
#endif

    fill_subevent_header(&g_local_event, ORIGIN_INITIATOR, local_mac, peer_mac, p_result, n_ap);
    fill_subevent_header(&g_peer_event, ORIGIN_REFLECTOR, peer_mac, local_mac, p_result, n_ap);

#if IS_ENABLED(CONFIG_MARS_CS_INLINE_PCT)
    cs_step_parse_inline(&g_local_event, &g_peer_event, p_local_steps, role);
#else
    cs_step_parse(&g_local_event, &g_peer_event, p_peer_steps, p_local_steps, role);
#endif

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
