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
static struct k_work_q        recovery_queue;
K_THREAD_STACK_DEFINE(recovery_stack, 2048);

/** @brief Run blocking recovery on its own queue, leaving the system queue free. */
static void recovery_handler(struct k_work * work)
{
    ARG_UNUSED(work);

    LOG_ERR("CS liveness watchdog: no completed procedure with step data for %u ms; forcing recovery",
            CONFIG_MARS_CS_LIVENESS_WATCHDOG_TIMEOUT_MS);

    if (gp_recovery)
    {
        gp_recovery();
    }
}

static K_WORK_DEFINE(recovery_work, recovery_handler);

/** @brief Timer expiry runs in interrupt context; defer all recovery work. */
static void timeout_handler(struct k_timer * timer)
{
    ARG_UNUSED(timer);

    (void)k_work_submit_to_queue(&recovery_queue, &recovery_work);
}

static K_TIMER_DEFINE(watchdog_timer, timeout_handler, NULL);

void cs_watchdog_init(cs_watchdog_recovery_t recovery)
{
    gp_recovery = recovery;
    k_work_queue_start(&recovery_queue, recovery_stack, K_THREAD_STACK_SIZEOF(recovery_stack), K_PRIO_PREEMPT(5), NULL);
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
