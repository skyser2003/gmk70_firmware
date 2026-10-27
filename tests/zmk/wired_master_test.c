/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wired_protocol.h"
struct fixture {
    uint8_t  replies[64], written[128];
    size_t   len, pos, sent;
    unsigned clears;
};
static bool receive(void *ctx, uint8_t *out, size_t n) {
    struct fixture *f         = ctx;
    size_t          available = f->len - f->pos, copied = n < available ? n : available;
    memcpy(out, f->replies + f->pos, copied);
    f->pos += copied;
    return copied == n;
}
static bool send(void *ctx, const uint8_t *in, size_t n) {
    struct fixture *f = ctx;
    assert(f->sent + n <= 128);
    memcpy(f->written + f->sent, in, n);
    f->sent += n;
    return true;
}
static void clear(void *ctx) {
    ++((struct fixture *)ctx)->clears;
}
int main(void) {
    struct fixture     f       = {0};
    struct gmk_wire_io io      = {&f, receive, send, clear};
    uint8_t            in[32]  = {42}, out[32];
    uint8_t            acks[4] = {13 ^ 19, 14 ^ 19, 15 ^ 19, 16 ^ 19};
    memcpy(f.replies, acks, 4);
    memset(f.replies + 4, 0x5a, 32);
    f.len = 36;
    assert(gmk_protocol_update(&io, in, out));
    assert(f.clears == 4 && f.sent == 41);
    assert(f.written[0] == 13 && f.written[2] == 18 && f.written[3] == 32 && f.written[4] == 32);
    assert(f.written[1] == gmk_wire_crc8(f.written + 2, 3));
    assert(f.written[5] == 14 && f.written[6] == 42 && f.written[38] == 15 && f.written[39] == 18 && f.written[40] == 16);
    for (unsigned i = 0; i < 32; ++i)
        assert(out[i] == 0x5a);
    for (size_t cut = 0; cut < 36; ++cut) {
        f.len = cut;
        f.pos = f.sent = f.clears = 0;
        memset(out, 99, 32);
        assert(!gmk_protocol_update(&io, in, out));
        for (unsigned i = 0; i < 32; ++i)
            assert(out[i] == 0);
    }
    for (unsigned bad = 0; bad < 4; ++bad) {
        f.len = 36;
        f.pos = f.sent = 0;
        f.replies[bad] ^= 1;
        assert(!gmk_protocol_update(&io, in, out));
        f.replies[bad] ^= 1;
        for (unsigned i = 0; i < 32; ++i)
            assert(out[i] == 0);
    }
    uint8_t  data[10] = {1, 0, 0, 1, 2, 0, 3, 0, 4, 0};
    uint16_t rows[5];
    f.replies[0] = 19;
    f.replies[1] = gmk_wire_crc8(data, 10);
    f.replies[2] = 1 ^ 19;
    memcpy(f.replies + 3, data, 10);
    f.len = 13;
    f.pos = f.sent = 0;
    assert(gmk_protocol_poll(&io, rows));
    assert(rows[0] == 1 && rows[1] == 256 && rows[4] == 4);
    f.replies[5] ^= 1;
    f.pos = f.sent = 0;
    assert(!gmk_protocol_poll(&io, rows));
    for (unsigned i = 0; i < 5; ++i)
        assert(rows[i] == 0);
    struct gmk_debounce debounce = {0};
    uint16_t            raw[5]   = {1};
    gmk_matrix_debounce(&debounce, raw, UINT32_MAX - 2);
    gmk_matrix_debounce(&debounce, raw, 2);
    assert(debounce.stable[0] == 1);
    struct gmk_peer peer = {0};
    gmk_peer_set(&peer, raw, UINT32_MAX - 10);
    assert(gmk_peer_rows(&peer, 88, rows));
    assert(!gmk_peer_rows(&peer, 89, rows));
    puts("wired master frame/partial response/checksum/wraparound tests passed");
}
