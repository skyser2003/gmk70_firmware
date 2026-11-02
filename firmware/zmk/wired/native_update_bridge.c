/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native_update_bridge.h"
#include "protocol.h"
#include <string.h>
#include <zephyr/sys/byteorder.h>

bool gmk_native_update_bridge_exchange(const struct gmk_wire_io *io, struct gmk_update_route *route, const uint8_t request[32], uint8_t response[32]) {
    memset(response, 0, 32);
    if (!route->known || (request[1] == 0x70 && (request[4] == GMK_INFO || request[4] == GMK_BEGIN))) {
        route->known = false;
        uint8_t probe[32], reply[32], expected[32];
        gmk_native_update_capability_request(probe);
        if (!gmk_protocol_split_command(io, probe, reply)) return false;
        (void)gmk_native_update_capability(probe, expected);
        if (!memcmp(reply, expected, 32)) {
            route->native = true;
        } else {
            /* QMK/Vial leaves its shared RPC reply scratch unchanged when
             * rejecting the 32-byte capability request. Prove legacy support
             * with a read-only STATUS request, never stale/zero scratch bytes.
             * STATUS returns a cached command reply (including BUSY/error),
             * so validate the service ABI rather than demanding a fresh INFO
             * completion. The actual caller retains normal async polling. */
            static const uint8_t status[32] = {GMK_UPDATE_COMMAND, 0x70, GMK_UPDATE_ABI, 1, GMK_STATUS};
            if (!gmk_protocol_update(io, status, reply) || memcmp(reply, status, 4) || reply[4] < GMK_STATUS || reply[4] > GMK_INFO || reply[5] > GMK_LOCKED || reply[6] > GMK_FAILED || reply[7] || sys_get_le32(reply + 24) != GMK_UPDATE_ABI) return false;
            route->native = false;
        }
        route->known = true;
    }
    /* Carrier is selected before delivery. Never retry/fall back after a
     * native request might have executed, especially INSTALL. */
    return route->native ? gmk_native_update_exchange(io, request, response) : gmk_protocol_update(io, request, response);
}
