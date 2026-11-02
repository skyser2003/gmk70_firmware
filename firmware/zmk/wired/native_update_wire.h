/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "wired_protocol.h"

typedef void (*gmk_update_wire_handler_t)(const uint8_t request[32], uint8_t response[32]);

/* Board extensions to native ZMK command/event framing. Existing packet ABI
 * stays inside the frame; the installed resident never parses this carrier. */
bool gmk_native_update_exchange(const struct gmk_wire_io *io, const uint8_t request[32], uint8_t response[32]);
bool gmk_native_update_serve(const struct gmk_wire_io *io, gmk_update_wire_handler_t handler);
/* Harmless RPC17 discovery: old applications reject its version and may leave
 * stale reply scratch. Do not send native frame bytes to an unknown peer. */
void gmk_native_update_capability_request(uint8_t request[32]);
bool gmk_native_update_capability(const uint8_t request[32], uint8_t response[32]);
