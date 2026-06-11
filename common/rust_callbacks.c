/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Shared Rust FFI callback implementations
 */

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app_main, LOG_LEVEL_INF);

/**
 * @brief Panic handler callback for the Rust runtime.
 *
 * Called by the Rust panic handler (libc-panic feature) when a Rust panic occurs.
 *
 * @param p_message  The panic message string from Rust.
 */
void rust_panic_cb(const char * p_message)
{
    LOG_ERR("%s\n", p_message);
}

/**
 * @brief Print callback for the Rust runtime.
 *
 * Called by the Rust printc module to output text from Rust code.
 *
 * @param p_message  The message string from Rust.
 */
void rust_print_cb(const char * p_message)
{
    LOG_INF("%s", p_message);
}
