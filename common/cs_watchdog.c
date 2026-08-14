/*
 * Copyright (c) 2026 Metirionic
 *
 * SPDX-License-Identifier: MIT
 */

#include "cs_watchdog.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app_main, LOG_LEVEL_INF);

static cs_watchdog_recovery_t gp_recovery;

/**
 * @brief Watchdog expiry — runs on the system workqueue.
 *
 * bt_conn_disconnect (from the recovery callback) blocks only on the HCI
 * command semaphore, released by the BT RX thread's command-complete
 * processing — the same hazard class as the existing GATT-discovery error
 * path in cs_initiator.c. serialize_send_log_message (initiator recovery) is
 * sem-gated with the give in the UARTE ISR, so it cannot deadlock either.
 */
static void timeout_handler(struct k_timer * timer)
{
    ARG_UNUSED(timer);

    LOG_ERR("CS liveness watchdog: no completed procedure with step data for %u ms; forcing recovery",
            CONFIG_MARS_CS_LIVENESS_WATCHDOG_TIMEOUT_MS);

    if (gp_recovery)
    {
        gp_recovery();
    }
}

static K_TIMER_DEFINE(watchdog_timer, timeout_handler, NULL);

void cs_watchdog_init(cs_watchdog_recovery_t recovery)
{
    gp_recovery = recovery;
}

void cs_watchdog_pet(void)
{
    if (!IS_ENABLED(CONFIG_MARS_CS_LIVENESS_WATCHDOG))
    {
        return;
    }

    if (IS_ENABLED(CONFIG_MARS_CS_LIVENESS_WATCHDOG_TEST_NO_PET))
    {
        /* Test hook (#116 desk bring-up): arm once on the first pet, never
         * re-arm — the watchdog fires ~timeout after the first healthy
         * procedure, deterministically exercising the recovery loop. */
        static bool armed;

        if (!armed)
        {
            armed = true;
            k_timer_start(&watchdog_timer, K_MSEC(CONFIG_MARS_CS_LIVENESS_WATCHDOG_TIMEOUT_MS), K_NO_WAIT);
        }
        return;
    }

    /* One-shot restart: lazy arm on the first pet, re-armed per pet. */
    k_timer_start(&watchdog_timer, K_MSEC(CONFIG_MARS_CS_LIVENESS_WATCHDOG_TIMEOUT_MS), K_NO_WAIT);
}
