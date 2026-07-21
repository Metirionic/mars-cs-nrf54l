/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Channel Sounding CSV serializer
 *
 * Replaces the former mars-bluetooth-hci COBS/postcard serializer with a plain
 * CSV text emitter. A populated cs_pct_procedure is stable-sorted by channel
 * ascending and written to the cobs-uart UART as one CSV line per Mode-2 step
 * (channel, magnitude, phase[, reflector magnitude, reflector phase]), with a
 * blank line separating procedures.
 */

#include "serialize.h"

#include <math.h>
#include <stdio.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "cs_initiator.h"

LOG_MODULE_DECLARE(app_main, LOG_LEVEL_INF);

/** @brief Size knob for the static TX buffer (CSV worst case ~8.4 KB; ample). */
#define CHUNK_SIZE 13000u

/** @brief TX buffer for CSV text. Leave margin for log messages. */
static uint8_t g_serialized[CHUNK_SIZE * 2u + 1000u];
/** @brief UART device used for CSV output (DT chosen: cobs_uart). */
static const struct device * gp_cobs_uart_dev = DEVICE_DT_GET(DT_CHOSEN(cobs_uart));

/** @brief UART TX-done gate: count 1 when no async TX is in progress.
 *
 * Taken before reusing g_serialized; given by the UART async callback on
 * UART_TX_DONE / UART_TX_ABORTED (and on uart_tx() error, since the TX never
 * started in that case). Keeps uart_tx from ever being called while a previous
 * transfer is still DMA-ing out of g_serialized.
 */
static K_SEM_DEFINE(sem_tx_done, 1, 1);

/** @brief One-time UART async init guard (callback registered on first TX). */
static bool g_uart_async_init_done;

/**
 * @brief UART async callback: signals TX completion to release sem_tx_done.
 *
 * TX-only path: only UART_TX_DONE and UART_TX_ABORTED are handled (RX events
 * are unused). k_sem_give() is ISR-safe, so the give from the UARTE ISR cannot
 * deadlock against the k_sem_take() performed in serialize_run(). UART_TX_ABORTED
 * is not expected in practice (no hardware flow control and uart_tx() uses
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

/* Human-readable labels for the CS config enum fields, emitted in the CSV
 * metadata block. Compact (no spaces) so each value is a single token for
 * downstream parsing. Index-clamping mirrors config_create_cb in
 * ble_callbacks.c. */
static const char * mode_str[5]       = {"Unused", "1(RTT)", "2(PBR)", "3(RTT+PBR)", "Invalid"};
static const char * role_str[3]       = {"Initiator", "Reflector", "Invalid"};
static const char * rtt_type_str[8]   = {"AA_only", "32bit_sounding", "96bit_sounding",
                                         "32bit_random", "64bit_random", "96bit_random",
                                         "128bit_random", "Invalid"};
static const char * phy_str[4]        = {"Invalid", "1M", "2M", "2M_2BT"};
static const char * chsel_type_str[3] = {"3b", "3c", "Invalid"};
static const char * ch3c_shape_str[3] = {"Hat", "X", "Invalid"};

/**
 * @brief Write the per-procedure CSV metadata block (4 #-prefixed lines).
 *
 * Emits the negotiated CS config, the configured procedure timing, and the
 * per-procedure result (subevent counts, steps, antenna paths) as #-prefixed
 * key=value lines ahead of this procedure's PCT data, so each captured
 * procedure is self-describing. The config and timing come from the saved
 * copies in cs_initiator.c (set at config creation / procedure enable); the
 * subevent counts and antenna paths come from the shared subevent-result
 * handler / latest subevent header.
 *
 * @param buf  Output buffer (g_serialized).
 * @param cap  Capacity of @p buf.
 * @param proc Populated procedure (procedure_counter, count, subevent counts).
 * @return Bytes written (the offset at which the caller appends data). 0 on
 *         snprintf failure/truncation (no metadata; caller writes from 0).
 */
static size_t format_procedure_metadata(uint8_t *                        buf,
                                        size_t                          cap,
                                        const struct cs_pct_procedure * proc)
{
    const struct bt_conn_le_cs_config *                    cfg = cs_initiator_get_cs_config();
    const struct bt_conn_le_cs_procedure_enable_complete * prm = cs_initiator_get_procedure_enable_complete();
    const struct bt_conn_le_cs_subevent_result *           hdr = cs_initiator_get_latest_subevent_header();

    uint8_t mode_idx       = cfg->mode > 0 && cfg->mode < 4 ? (uint8_t)cfg->mode : 4;
    uint8_t role_idx       = MIN(cfg->role, 2);
    uint8_t rtt_type_idx   = MIN(cfg->rtt_type, 7);
    uint8_t phy_idx        = cfg->cs_sync_phy > 0 && cfg->cs_sync_phy < 4 ? (uint8_t)cfg->cs_sync_phy : 0;
    uint8_t chsel_type_idx = MIN(cfg->channel_selection_type, 2);
    uint8_t ch3c_shape_idx = MIN(cfg->ch3c_shape, 2);

    int n = snprintf((char *)buf, cap,
        "# procedure=%u config_id=%u\n"
        "# config: mode=%s min_main_mode_steps=%u max_main_mode_steps=%u main_mode_repetition=%u mode_0_steps=%u role=%s rtt_type=%s cs_sync_phy=%s channel_map_repetition=%u channel_selection_type=%s ch3c_shape=%s ch3c_jump=%u cs_enhancements_1=%u t_ip1_us=%u t_ip2_us=%u t_fcs_us=%u t_pm_us=%u channel_map=0x%08X%08X%04X\n"
        "# timing: config_id=%u tone_antenna_config_selection=%u selected_tx_power=%d subevent_len_us=%u subevents_per_event=%u subevent_interval=%u event_interval=%u procedure_interval=%u procedure_count=%u max_procedure_len=%u\n"
        "# result: subevents_total=%u subevents_aborted=%u steps=%u num_antenna_paths=%u\n",
        (unsigned)proc->procedure_counter,
        (unsigned)cfg->id,
        mode_str[mode_idx],
        (unsigned)cfg->min_main_mode_steps,
        (unsigned)cfg->max_main_mode_steps,
        (unsigned)cfg->main_mode_repetition,
        (unsigned)cfg->mode_0_steps,
        role_str[role_idx],
        rtt_type_str[rtt_type_idx],
        phy_str[phy_idx],
        (unsigned)cfg->channel_map_repetition,
        chsel_type_str[chsel_type_idx],
        ch3c_shape_str[ch3c_shape_idx],
        (unsigned)cfg->ch3c_jump,
        (unsigned)cfg->cs_enhancements_1,
        (unsigned)cfg->t_ip1_time_us,
        (unsigned)cfg->t_ip2_time_us,
        (unsigned)cfg->t_fcs_time_us,
        (unsigned)cfg->t_pm_time_us,
        (unsigned)sys_get_le32(&cfg->channel_map[6]),
        (unsigned)sys_get_le32(&cfg->channel_map[2]),
        (unsigned)sys_get_le16(&cfg->channel_map[0]),
        (unsigned)prm->config_id,
        (unsigned)prm->tone_antenna_config_selection,
        (int)prm->selected_tx_power,
        (unsigned)prm->subevent_len,
        (unsigned)prm->subevents_per_event,
        (unsigned)prm->subevent_interval,
        (unsigned)prm->event_interval,
        (unsigned)prm->procedure_interval,
        (unsigned)prm->procedure_count,
        (unsigned)prm->max_procedure_len,
        (unsigned)proc->subevents_total,
        (unsigned)proc->subevents_aborted,
        (unsigned)proc->count,
        (unsigned)hdr->header.num_antenna_paths);

    if (n < 0 || (size_t)n >= cap)
    {
        /* snprintf failure or truncation: emit no metadata, start data at 0. */
        return 0;
    }
    return (size_t)n;
}

/**
 * @brief Format a populated cs_pct_procedure as CSV and transmit over UART.
 *
 * Stable-sorts @p proc's samples by channel ascending (in place; equal channels
 * keep their original step order), then writes one CSV line per sample into the
 * static g_serialized[] buffer and transmits it via the cobs-uart UART. Each
 * line is `channel, init_mag, init_phase` (IPT) or
 * `channel, init_mag, init_phase, refl_mag, refl_phase` (RAS), with magnitude =
 * sqrtf(i*i + q*q) and phase = atan2f(q, i) in radians, floats as %.6f. A blank
 * line follows the block as a procedure separator. An empty procedure (count ==
 * 0) emits nothing (no lines, no separator).
 *
 * Ahead of each non-empty procedure's data lines, format_procedure_metadata()
 * writes 4 #-prefixed lines (procedure header, CS config, configured timing,
 * per-procedure result incl. subevent counts) so each captured procedure block
 * is self-describing. A reader can recover the original pure-numeric CSV with
 * `grep -v '^#'`.
 *
 * @param proc  Populated procedure to serialize.
 */
void serialize_run(struct cs_pct_procedure * proc)
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
     * In steady state the prior TX finishes before the next procedure completes,
     * so this returns immediately; under jitter it blocks only for the brief
     * overshoot, in the quiet gap before the next procedure's first subevent.
     */
    k_sem_take(&sem_tx_done, K_FOREVER);

    LOG_INF("Run serialization for procedure %u", proc->procedure_counter);

    if (proc->count == 0)
    {
        /* Empty/aborted procedure: emit nothing (no data lines, no separator). */
        k_sem_give(&sem_tx_done);
        return;
    }

    /* Stable insertion sort by channel ascending — equal channels keep step
     * order (insertion sort is stable; do not switch to qsort without a
     * tiebreak). In-place on the caller's (static) procedure buffer, which is
     * re-populated each procedure. */
    for (size_t i = 1; i < proc->count; i++)
    {
        struct cs_pct_sample key = proc->samples[i];
        size_t                j  = i;
        while (j > 0 && proc->samples[j - 1].channel > key.channel)
        {
            proc->samples[j] = proc->samples[j - 1];
            j--;
        }
        proc->samples[j] = key;
    }

    /* Per-procedure metadata block (#-prefixed) ahead of the PCT data lines. */
    size_t off = format_procedure_metadata(g_serialized, sizeof(g_serialized), proc);

    for (size_t i = 0; i < proc->count; i++)
    {
        const struct cs_pct_sample * s = &proc->samples[i];
        float init_mag = sqrtf(s->init_i * s->init_i + s->init_q * s->init_q);
        float init_phi = atan2f(s->init_q, s->init_i);
        int   n;

#if IS_ENABLED(CONFIG_BT_RAS_RREQ)
        float refl_mag = sqrtf(s->refl_i * s->refl_i + s->refl_q * s->refl_q);
        float refl_phi = atan2f(s->refl_q, s->refl_i);
        n = snprintf(&g_serialized[off], sizeof(g_serialized) - off,
                     "%u,%.6f,%.6f,%.6f,%.6f\n",
                     (unsigned int)s->channel, init_mag, init_phi, refl_mag, refl_phi);
#else
        n = snprintf(&g_serialized[off], sizeof(g_serialized) - off,
                     "%u,%.6f,%.6f\n",
                     (unsigned int)s->channel, init_mag, init_phi);
#endif
        if (n < 0)
        {
            LOG_ERR("CSV snprintf failed at step %zu (err %d)", i, n);
            break;
        }
        /* snprintf returns the would-be length; if it would overflow the
         * remaining buffer, stop (the truncated write is never transmitted). */
        if ((size_t)n >= sizeof(g_serialized) - off)
        {
            LOG_ERR("CSV buffer overflow at step %zu (need %d, have %zu)",
                    i, n, sizeof(g_serialized) - off);
            break;
        }
        off += (size_t)n;
    }

    /* Blank line separates this procedure's CSV block from the next. */
    if (off + 1u < sizeof(g_serialized))
    {
        g_serialized[off++] = '\n';
    }

    LOG_INF("Sending %u bytes", (unsigned int)off);

    int err = uart_tx(gp_cobs_uart_dev, g_serialized, off, SYS_FOREVER_MS);

    if (err)
    {
        LOG_ERR("UART TX failed (err %d)", err);
        /* TX never started: release the gate so the next run does not hang. */
        k_sem_give(&sem_tx_done);
    }
}