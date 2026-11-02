/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "native_update_bridge.h"
#include <zephyr/sys/byteorder.h>

static bool     legacy_peer, fail_discovery, malformed_discovery, fail_native;
static unsigned probes, native_calls, legacy_calls, legacy_probes;
static uint8_t  legacy_scratch[32];
static int      bad_legacy_info = -1;
static bool     legacy_info_pending;
bool            gmk_protocol_split_command(const struct gmk_wire_io *io, const uint8_t request[32], uint8_t response[32]) {
    (void)io;
    ++probes;
    memcpy(response, legacy_scratch, 32);
    if (fail_discovery) return false;
    if (!legacy_peer) assert(gmk_native_update_capability(request, response));
    if (malformed_discovery) response[5] ^= 1;
    return true;
}
bool gmk_protocol_update(const struct gmk_wire_io *io, const uint8_t request[32], uint8_t response[32]) {
    (void)io;
    if (request[4] == 1 && !request[8])
        ++legacy_probes;
    else
        ++legacy_calls;
    if (!legacy_peer) return false;
    memset(response, 0, 32);
    memcpy(response, request, 5);
    if (request[4] == 7) {
        response[5]         = 1; /* Legacy INFO queues work and immediately returns BUSY. */
        legacy_info_pending = true;
    } else if (request[4] == 1 && legacy_info_pending) {
        response[4]         = 7; /* STATUS returns the completed cached INFO reply. */
        legacy_info_pending = false;
    }
    sys_put_le32(1, response + 24);
    if (bad_legacy_info >= 0 && request[4] == 1 && !request[8]) response[bad_legacy_info] ^= 0x80;
    memcpy(legacy_scratch, response, 32);
    return true;
}
static bool native_write(void *ctx, const uint8_t *data, size_t size) {
    (void)ctx;
    (void)data;
    (void)size;
    ++native_calls;
    return !fail_native;
}
static bool native_read(void *ctx, uint8_t *data, size_t size) {
    (void)ctx;
    (void)data;
    (void)size;
    return false;
}
static void clear(void *ctx) {
    (void)ctx;
}
int main(void) {
    const struct gmk_wire_io io          = {NULL, native_read, native_write, clear};
    struct gmk_update_route  route       = {0};
    uint8_t                  request[32] = {0x7f, 0x70, 1, 1, 7}, response[32];
    legacy_peer                          = true;
    request[8]                           = 0x99;
    assert(gmk_native_update_bridge_exchange(&io, &route, request, response));
    assert(probes == 1 && legacy_calls == 1 && native_calls == 0 && response[0] == 0x7f);
    request[4] = 2; /* QMK/Vial retains the prior INFO reply in RPC17 scratch. */
    assert(gmk_native_update_bridge_exchange(&io, &route, request, response));
    assert(probes == 2 && legacy_probes == 2 && legacy_calls == 2);
    request[4] = 3; /* DATA retains the discovered carrier. */
    assert(gmk_native_update_bridge_exchange(&io, &route, request, response));
    assert(probes == 2 && legacy_calls == 3 && !native_calls);
    legacy_peer = false;
    request[4]  = 7; /* INFO rediscovers the peer after its application restart. */
    fail_native = true;
    assert(!gmk_native_update_bridge_exchange(&io, &route, request, response));
    assert(probes == 3 && native_calls == 1 && legacy_calls == 3);
    request[4] = 5; /* Uncertain INSTALL never falls back to legacy or retries. */
    assert(!gmk_native_update_bridge_exchange(&io, &route, request, response));
    assert(probes == 3 && native_calls == 2 && legacy_calls == 3);
    fail_discovery = true;
    request[4]     = 2;
    assert(!gmk_native_update_bridge_exchange(&io, &route, request, response));
    assert(native_calls == 2 && legacy_calls == 3);
    assert(!route.known);
    fail_discovery      = false;
    malformed_discovery = true;
    assert(!gmk_native_update_bridge_exchange(&io, &route, request, response));
    assert(native_calls == 2 && legacy_calls == 3);
    for (unsigned i = 0; i < 32; ++i)
        assert(response[i] == 0);
    legacy_peer                     = true;
    malformed_discovery             = false;
    const int invalid_info_fields[] = {0, 1, 2, 3, 4, 5, 6, 7, 24, 25, 26, 27};
    for (unsigned i = 0; i < sizeof(invalid_info_fields) / sizeof(invalid_info_fields[0]); ++i) {
        bad_legacy_info = invalid_info_fields[i];
        assert(!gmk_native_update_bridge_exchange(&io, &route, request, response));
        assert(!route.known && native_calls == 2 && legacy_calls == 3);
    }
    puts("Native updater bridge: safe discovery, legacy/native routing and no ambiguous fallback passed");
}
