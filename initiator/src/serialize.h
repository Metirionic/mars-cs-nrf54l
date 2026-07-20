/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 *  @brief Channel Sounding CSV serializer
 */

#ifndef SERIALIZE_H__
#define SERIALIZE_H__

#include "cs_pct.h"

/**
 * @brief Serialize a populated cs_pct_procedure as CSV and transmit over UART.
 *
 * Stable-sorts the procedure's samples by channel ascending, formats each as a
 * CSV line — `channel, init_mag, init_phase` in IPT builds, or
 * `channel, init_mag, init_phase, refl_mag, refl_phase` in RAS builds — and
 * writes the block to the cobs-uart UART followed by a blank-line procedure
 * separator. Magnitude/phase are in radians via sqrtf/atan2f, floats as %.6f.
 * An empty procedure (count == 0) emits nothing (no lines, no separator).
 *
 * @param proc  Populated procedure to serialize.
 */
void serialize_run(struct cs_pct_procedure * proc);

#endif /* SERIALIZE_H__ */