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

/** @brief Max COBS-serialized size of ONE SubeventResultEvent (160 steps).
 *
 * Sized for mars-bluetooth-hci 0.13.2: ModeRoleSpecificInfo serializes a
 * mode0 field (quality, RSSI, antenna, measured_freq_offset u16) on EVERY
 * step alongside the mode1/mode2 fields, so sizes now depend on values
 * (postcard varints: the freq offset is 1 B when 0, 3 B for the 0xC000
 * sentinel). Measured per-event COBS sizes (host harness, 160 steps):
 *   13,302 B  zeroed (lower bound)
 *   13,955 B  realistic FW population (real MACs, populated mode0 + mode2)
 *   13,968 B  worst case (every varint maximized)
 * 14,000 B keeps headroom over the measured worst case; g_serialized below
 * holds two events plus a log message plus ~1 KB margin. 0.8.0 was ~8,640
 * B/event with no mode1 field; 0.12.0 was 12,502 B/event zeroed.
 */
#define CHUNK_SIZE 14000u

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
static int serialize_append_log_message(const char * p_message)
{
    SerializedData_t result = serialize_log_message((int8_t const *)p_message, true);

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
 * @brief Register the async UART callback once, on first use.
 *
 * @return 0 on success, negative errno on error.
 */
static int serialize_uart_init(void)
{
    if (g_uart_async_init_done)
    {
        return 0;
    }

    if (!device_is_ready(gp_cobs_uart_dev))
    {
        LOG_ERR("COBS UART device not ready");
        return -ENODEV;
    }

    int cb_err = uart_callback_set(gp_cobs_uart_dev, cobs_uart_async_cb, NULL);
    if (cb_err)
    {
        LOG_ERR("uart_callback_set failed (err %d)", cb_err);
        return cb_err;
    }

    g_uart_async_init_done = true;
    return 0;
}

/**
 * @brief Replace the in-flight buffer with a single log frame and transmit.
 *
 * Called while the TX gate is held (from serialize_run's failure paths).
 * The buffer may hold an unusable partial frame, so it is reset before
 * appending the marker. Returns the uart_tx result: on success the UART ISR
 * releases the gate; on failure the caller must.
 */
static int serialize_send_log_message_locked(const char * p_message)
{
    serialize_init();
    (void)serialize_append_log_message(p_message);
    return uart_tx(gp_cobs_uart_dev, g_serialized, g_total_written_size, SYS_FOREVER_MS);
}

int serialize_send_log_message(const char * p_message)
{
    int err = serialize_uart_init();
    if (err)
    {
        return err;
    }

    /* Gate: same buffer-protection rule as serialize_run — do not reuse
     * g_serialized until the previous async TX completed. In the normal
     * fault case no transfer is in flight and this returns immediately.
     */
    k_sem_take(&sem_tx_done, K_FOREVER);

    serialize_init();

    err = serialize_append_log_message(p_message);
    if (err)
    {
        k_sem_give(&sem_tx_done);
        return err;
    }

    err = uart_tx(gp_cobs_uart_dev, g_serialized, g_total_written_size, SYS_FOREVER_MS);
    if (err)
    {
        /* TX never started: release the gate so the next run does not hang. */
        k_sem_give(&sem_tx_done);
    }

    return err;
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
    if (serialize_uart_init())
    {
        return;
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
        /* Fault-path diagnostic: the buffer holds a partial event frame
         * that would not decode on the host, so replace it with a standalone
         * log frame (e.g. the -12 overflow from a wire-format size mismatch).
         */
        if (serialize_send_log_message_locked("Serialize error: local event") < 0)
        {
            k_sem_give(&sem_tx_done);
        }
        return;
    }

    err = serialize_append_event(p_peer_event);
    if (err)
    {
        LOG_ERR("Failed to serialize peer event (err %d)", err);
        if (serialize_send_log_message_locked("Serialize error: peer event") < 0)
        {
            k_sem_give(&sem_tx_done);
        }
        return;
    }

    err = serialize_append_log_message("Processing finished");
    if (err)
    {
        LOG_ERR("Failed to serialize log (err %d)", err);
        if (serialize_send_log_message_locked("Serialize error: log") < 0)
        {
            k_sem_give(&sem_tx_done);
        }
        return;
    }

    err = uart_tx(gp_cobs_uart_dev, g_serialized, g_total_written_size, SYS_FOREVER_MS);

    LOG_INF("Sending %u bytes", g_total_written_size);

    if (err)
    {
        LOG_ERR("UART TX failed (err %d)", err);
        /* TX never started: the buffer still holds valid frames — append a
         * failure marker and retry once so the host sees the cause instead
         * of a silent gap. */
        (void)serialize_append_log_message("UART TX error");
        err = uart_tx(gp_cobs_uart_dev, g_serialized, g_total_written_size, SYS_FOREVER_MS);
        if (err)
        {
            /* Second failure: release the gate so the next run does not hang. */
            k_sem_give(&sem_tx_done);
        }
    }
}
