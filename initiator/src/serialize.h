/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SERIALIZE_H__
#define SERIALIZE_H__

#include "rust_ffi_types.h"

/**
 * @brief Serialize populated SubeventResultEvents and transmit over UART COBS.
 *
 * @param p_local_event  Populated initiator SubeventResultEvent.
 * @param p_peer_event   Populated reflector SubeventResultEvent.
 */
void serialize_run(SubeventResultEvent_t * p_local_event, SubeventResultEvent_t * p_peer_event);

#endif /* SERIALIZE_H__ */