/*
 * Copyright (c) 2026 Metirionic
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief CS liveness watchdog — recovers from permanent CS abort-storms (#116)
 *
 *  During the #116 abort-storm every procedure's subevents abort at the LL
 *  while the BLE ACL + RAS GATT path stays healthy: empty Ranging Data frames
 *  keep flowing at ~5/s, so a watchdog keyed on link state or frame flow never
 *  fires. This watchdog instead keys on *completed procedures with step data*
 *  and forces the existing disconnect -> reboot-on-disconnect recovery when
 *  none arrive within CONFIG_MARS_CS_LIVENESS_WATCHDOG_TIMEOUT_MS.
 *
 *  The watchdog is inert until the first pet (lazy arm), so the connection
 *  handshake and any pre-CS gaps never false-trigger it.
 */

#ifndef CS_WATCHDOG_H
#define CS_WATCHDOG_H

/**
 * @brief Recovery callback invoked on watchdog expiry (system workqueue context).
 *
 * Must force recovery: disconnect the connection (both boards reboot via the
 * existing disconnected_cb -> sys_reboot path) or reboot directly.
 */
typedef void (*cs_watchdog_recovery_t)(void);

/**
 * @brief Initialize the CS liveness watchdog.
 *
 * @param recovery  Callback invoked on expiry; may be NULL (log-only).
 */
void cs_watchdog_init(cs_watchdog_recovery_t recovery);

/**
 * @brief Pet the watchdog — restart the timeout.
 *
 * Called once per completed CS procedure with step data. The first pet arms
 * the timer; every pet re-arms it. With
 * CONFIG_MARS_CS_LIVENESS_WATCHDOG_TEST_NO_PET=y this is a no-op (test hook
 * for deterministically exercising the recovery loop).
 */
void cs_watchdog_pet(void);

#endif /* CS_WATCHDOG_H */
