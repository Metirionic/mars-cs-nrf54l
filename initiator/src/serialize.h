/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SERIALIZE_H__
#define SERIALIZE_H__

#include "mars_bluetooth_hci.h"

/**
 * @brief Serialize populated SubeventResultEvents and transmit over UART COBS.
 *
 * @param p_local_event  Populated initiator SubeventResultEvent.
 * @param p_peer_event   Populated reflector SubeventResultEvent.
 */
void serialize_run(SubeventResultEvent_t * p_local_event, SubeventResultEvent_t * p_peer_event);

/**
 * @brief Serialize a log message and transmit it over UART COBS.
 *
 * Fault-path diagnostic entry point for events outside serialize_run
 * (e.g. the ranging data error path): the stall trigger that stops the
 * event stream is emitted here so a host capturing only the COBS UART can
 * see it. Honors the same UART TX gate as serialize_run.
 *
 * @param p_message  NUL-terminated message.
 * @return 0 on success, negative errno on error.
 */
int serialize_send_log_message(const char * p_message);

#endif /* SERIALIZE_H__ */
