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

#include "subevent.h"

LOG_MODULE_DECLARE(app_main, LOG_LEVEL_INF);

#include "mars_bluetooth_hci.h"

/** @brief Max serialized size per event pair (160 steps + header overhead). */
#define CHUNK_SIZE 13000u

/** @brief TX buffer for COBS-encoded serialized data. Leave margin for log messages. */
static uint8_t g_serialized[CHUNK_SIZE * 2u + 1000u];
/** @brief Total bytes written to g_serialized for the current run. */
static size_t g_total_written_size = 0u;
/** @brief UART device used for COBS output. */
static const struct device * gp_cobs_uart_dev = DEVICE_DT_GET(DT_CHOSEN(cobs_uart));

/** @brief UART TX-done gate: count 1 when no async TX is in progress.
 *
 * Taken before reusing g_serialized; given by the UART async callback on
 * UART_TX_DONE / UART_TX_ABORTED (and on uart_tx() error, since the TX never
 * started in that case). Mirrors the sem_local_steps "buffer available" pattern
 * in common/cs_initiator.c, keeping uart_tx from ever being called while a
 * previous transfer is still DMA-ing out of g_serialized.
 */
static K_SEM_DEFINE(sem_tx_done, 1, 1);

/** @brief One-time UART async init guard (callback registered on first TX). */
static bool g_uart_async_init_done;

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
 * @brief UART async callback: signals TX completion to release sem_tx_done.
 *
 * TX-only path: only UART_TX_DONE and UART_TX_ABORTED are handled (RX events
 * are unused). k_sem_give() is ISR-safe, so the give from the UARTE ISR cannot
 * deadlock against the k_sem_take() performed in serialize_run() (BT RX
 * workqueue context). UART_TX_ABORTED is not expected in practice (no
 * hardware flow control on the COBS UART overlays and uart_tx() uses
 * SYS_FOREVER_MS), but is handled defensively so the gate never sticks.
 */
static void cobs_uart_async_cb(const struct device * p_dev, struct uart_event * p_evt, void * p_user_data)
{
    ARG_UNUSED(p_dev);
    ARG_UNUSED(p_user_data);

    switch (p_evt->type)
    {
    case UART_TX_DONE:
        k_sem_give(&sem_tx_done);
        break;
    case UART_TX_ABORTED:
        LOG_WRN("COBS UART TX aborted (%u bytes sent)", (unsigned)p_evt->data.tx.len);
        k_sem_give(&sem_tx_done);
        break;
    default:
        /* RX events unused (TX-only path). */
        break;
    }
}

/**
 * @brief Serialize populated SubeventResultEvents and transmit over UART.
 *
 * COBS-encodes two SubeventResultEvent structures (initiator + reflector)
 * via the Rust FFI and transmits the COBS-encoded binary over the configured
 * UART device. The events must already be populated (via subevent_populate
 * or subevent_populate_inline) before calling this function.
 *
 * @param p_local_event  Populated initiator SubeventResultEvent.
 * @param p_peer_event   Populated reflector SubeventResultEvent.
 */
void serialize_run(SubeventResultEvent_t * p_local_event, SubeventResultEvent_t * p_peer_event)
{
    if (!g_uart_async_init_done)
    {
        if (!device_is_ready(gp_cobs_uart_dev))
        {
            LOG_ERR("COBS UART device not ready");
            return;
        }
        int cb_err = uart_callback_set(gp_cobs_uart_dev, cobs_uart_async_cb, NULL);
        if (cb_err)
        {
            LOG_ERR("uart_callback_set failed (err %d)", cb_err);
            return;
        }
        g_uart_async_init_done = true;
    }

    /* Gate: do not reuse g_serialized until the previous async TX completed.
     * In steady state the prior TX finishes ~1-16 ms before the next procedure
     * completes, so this returns immediately; under jitter it blocks only for
     * the brief overshoot, in the quiet gap before the next procedure's first
     * subevent. The sem is given by cobs_uart_async_cb() from the UARTE ISR.
     */
    k_sem_take(&sem_tx_done, K_FOREVER);

    serialize_init();

    LOG_INF("Run serialization for procedure %u", p_local_event->initial_meta.procedure_counter);

    int err;

    err = serialize_append_event(p_local_event);
    if (err)
    {
        LOG_ERR("Failed to serialize local event (err %d)", err);
        k_sem_give(&sem_tx_done);
        return;
    }

    err = serialize_append_event(p_peer_event);
    if (err)
    {
        LOG_ERR("Failed to serialize peer event (err %d)", err);
        k_sem_give(&sem_tx_done);
        return;
    }

    err = serialize_append_log_message("Processing finished");
    if (err)
    {
        LOG_ERR("Failed to serialize log (err %d)", err);
        k_sem_give(&sem_tx_done);
        return;
    }

    err = uart_tx(gp_cobs_uart_dev, g_serialized, g_total_written_size, SYS_FOREVER_MS);

    LOG_INF("Sending %u bytes", g_total_written_size);

    if (err)
    {
        LOG_ERR("UART TX failed (err %d)", err);
        /* TX never started: release the gate so the next run does not hang. */
        k_sem_give(&sem_tx_done);
    }
}
