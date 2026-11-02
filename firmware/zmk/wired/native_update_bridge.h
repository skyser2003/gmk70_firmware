/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "native_update_wire.h"

struct gmk_update_route {
    bool known, native;
};
bool gmk_native_update_bridge_exchange(const struct gmk_wire_io *io, struct gmk_update_route *route, const uint8_t request[32], uint8_t response[32]);
