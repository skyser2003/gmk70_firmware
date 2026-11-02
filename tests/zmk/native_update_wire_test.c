/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "native_update_wire.h"
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

struct fixture {
    uint8_t  request[64], response[64];
    size_t   request_size, response_size, request_pos, response_pos;
    size_t   request_cut, response_cut;
    int      corrupt_request, corrupt_response;
    bool     rewrite_crc;
    unsigned calls, writes, clears;
};
static struct fixture *active;
static void            receiver(const uint8_t request[32], uint8_t response[32]) {
    ++active->calls;
    for (unsigned i = 0; i < 32; ++i)
        response[i] = request[i] ^ 0xa5;
}
static bool server_read(void *ctx, uint8_t *out, size_t count) {
    struct fixture *fixture = ctx;
    size_t          limit   = fixture->request_size < fixture->request_cut ? fixture->request_size : fixture->request_cut;
    if (count > limit - fixture->request_pos) return false;
    memcpy(out, fixture->request + fixture->request_pos, count);
    fixture->request_pos += count;
    return true;
}
static bool server_write(void *ctx, const uint8_t *data, size_t count) {
    struct fixture *fixture = ctx;
    assert(count <= sizeof(fixture->response));
    memcpy(fixture->response, data, count);
    fixture->response_size = count;
    if (fixture->corrupt_response >= 0) fixture->response[fixture->corrupt_response / 8] ^= 1U << (fixture->corrupt_response % 8);
    if (fixture->rewrite_crc) sys_put_le32(crc32_ieee(fixture->response, count - 4), fixture->response + count - 4);
    return true;
}
static void clear(void *ctx) {
    ++((struct fixture *)ctx)->clears;
}
static bool client_write(void *ctx, const uint8_t *data, size_t count) {
    struct fixture *fixture = ctx;
    assert(count <= sizeof(fixture->request));
    ++fixture->writes;
    memcpy(fixture->request, data, count);
    fixture->request_size = count;
    if (fixture->corrupt_request >= 0) fixture->request[fixture->corrupt_request / 8] ^= 1U << (fixture->corrupt_request % 8);
    if (fixture->rewrite_crc) sys_put_le32(crc32_ieee(fixture->request, count - 4), fixture->request + count - 4);
    struct gmk_wire_io server = {ctx, server_read, server_write, clear};
    (void)gmk_native_update_serve(&server, receiver);
    return true;
}
static bool client_read(void *ctx, uint8_t *out, size_t count) {
    struct fixture *fixture = ctx;
    size_t          limit   = fixture->response_size < fixture->response_cut ? fixture->response_size : fixture->response_cut;
    if (count > limit - fixture->response_pos) return false;
    memcpy(out, fixture->response + fixture->response_pos, count);
    fixture->response_pos += count;
    return true;
}
static bool exchange(struct fixture *fixture, uint8_t response[32]) {
    uint8_t request[32];
    for (unsigned i = 0; i < 32; ++i)
        request[i] = (uint8_t)i;
    struct gmk_wire_io client = {fixture, client_read, client_write, clear};
    active                    = fixture;
    bool result               = gmk_native_update_exchange(&client, request, response);
    if (result)
        for (unsigned i = 0; i < 32; ++i)
            assert(response[i] == (request[i] ^ 0xa5));
    else
        for (unsigned i = 0; i < 32; ++i)
            assert(response[i] == 0);
    return result;
}
static struct fixture fresh(void) {
    return (struct fixture){.request_cut = 64, .response_cut = 64, .corrupt_request = -1, .corrupt_response = -1};
}
int main(void) {
    uint8_t        response[32];
    struct fixture fixture = fresh();
    assert(exchange(&fixture, response));
    assert(fixture.calls == 1 && fixture.writes == 1);
    assert(fixture.request_size == 43 && fixture.response_size == 46);
    assert(!memcmp(fixture.request, "ZmKw", 4) && fixture.request[4] == 34 && fixture.request[5] == 0 && fixture.request[6] == 0x80);
    assert(fixture.response[4] == 37 && fixture.response[5] == 0 && fixture.response[6] == 0x80 && fixture.response[7] == 0 && fixture.response[8] == 0 && fixture.response[9] == 0);
    for (int bit = 0; bit < 43 * 8; ++bit) {
        fixture                 = fresh();
        fixture.corrupt_request = bit;
        assert(!exchange(&fixture, response) && !fixture.calls && fixture.writes == 1);
    }
    for (int bit = 0; bit < 46 * 8; ++bit) {
        fixture                  = fresh();
        fixture.corrupt_response = bit;
        assert(!exchange(&fixture, response) && fixture.calls == 1 && fixture.writes == 1);
    }
    for (size_t cut = 0; cut < 43; ++cut) {
        fixture             = fresh();
        fixture.request_cut = cut;
        assert(!exchange(&fixture, response) && !fixture.calls);
    }
    for (size_t cut = 0; cut < 46; ++cut) {
        fixture              = fresh();
        fixture.response_cut = cut;
        assert(!exchange(&fixture, response) && fixture.calls == 1);
    }
    for (int field = 5; field <= 6; ++field) {
        fixture                 = fresh();
        fixture.corrupt_request = field * 8;
        fixture.rewrite_crc     = true;
        assert(!exchange(&fixture, response) && !fixture.calls);
    }
    for (int field = 5; field <= 9; ++field) {
        fixture                  = fresh();
        fixture.corrupt_response = field * 8;
        fixture.rewrite_crc      = true;
        assert(!exchange(&fixture, response) && fixture.calls == 1);
    }
    puts("Native updater wire: real ZMK parser, round trip, every-bit corruption, truncation and no replay passed");
}
