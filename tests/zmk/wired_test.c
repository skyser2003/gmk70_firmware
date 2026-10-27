/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wired_protocol.h"

uint8_t crc8(const void *, size_t);
struct fixture {
    uint8_t  rx[128], tx[128];
    size_t   n, pos, sent;
    unsigned clears, calls;
};
static bool read_bytes(void *ctx, uint8_t *out, size_t n) {
    struct fixture *f = ctx;
    if (n > f->n - f->pos) return false;
    memcpy(out, f->rx + f->pos, n);
    f->pos += n;
    return true;
}
static bool write_bytes(void *ctx, const uint8_t *in, size_t n) {
    struct fixture *f = ctx;
    assert(f->sent + n <= sizeof(f->tx));
    memcpy(f->tx + f->sent, in, n);
    f->sent += n;
    return true;
}
static void clear(void *ctx) {
    struct fixture *f = ctx;
    ++f->clears;
    f->pos = f->n;
}
static unsigned calls;
static void     update(const uint8_t in[32], uint8_t out[32]) {
    ++calls;
    out[0] = in[0] + 1;
}
static bool server(struct gmk_protocol *p, struct fixture *f, const uint8_t *data, size_t n) {
    memcpy(f->rx, data, n);
    f->n   = n;
    f->pos = f->sent      = 0;
    struct gmk_wire_io io = {f, read_bytes, write_bytes, clear};
    return gmk_protocol_serve(p, &io);
}
int main(void) {
    struct gmk_protocol p = {0};
    struct fixture      f = {0};
    uint8_t             sequence[64];
    for (unsigned i = 0; i < sizeof(sequence); ++i)
        sequence[i] = (uint8_t)i;
    for (size_t n = 0; n <= sizeof(sequence); ++n)
        assert(gmk_wire_crc8(sequence, n) == crc8(sequence, n));
    assert(gmk_wire_crc8(NULL, 0) == 255);
    uint16_t rows[5] = {1, 0x100, 3, 4, 0x101};
    gmk_protocol_matrix(&p, rows);
    uint8_t id = 1;
    assert(server(&p, &f, &id, 1));
    assert(f.sent == 11 && f.tx[0] == (1 ^ 19));
    assert(f.tx[1] == 1 && f.tx[3] == 0 && f.tx[4] == 1);
    id = 19;
    assert(!server(&p, &f, &id, 1));
    assert(f.sent == 0);
    const uint8_t sizes[13] = {0, 0, 10, 0, 0, 0, 4, 1, 1, 1, 4, 16, 12};
    for (id = 0; id < 13; ++id) {
        uint8_t frame[33] = {0};
        frame[0]          = id;
        assert(server(&p, &f, frame, 1 + sizes[id]));
    }
    uint8_t info[5] = {13, 0, 18, 32, 32};
    info[1]         = gmk_wire_crc8(info + 2, 3);
    p.update        = update;
    assert(server(&p, &f, info, 5));
    uint8_t request[33] = {14, 42};
    assert(server(&p, &f, request, 33));
    uint8_t exec[2] = {15, 18};
    assert(server(&p, &f, exec, 2));
    assert(calls == 1);
    id = 16;
    assert(server(&p, &f, &id, 1));
    assert(f.sent == 33 && f.tx[1] == 43);
    for (size_t i = 2; i < 33; ++i)
        assert(f.tx[i] == 0);
    /* Consumed response cannot expose old output. */
    assert(!server(&p, &f, &id, 1));
    info[1] ^= 1;
    assert(!server(&p, &f, info, 5));
    assert(!server(&p, &f, exec, 2));
    assert(calls == 1);
    info[1] ^= 1;
    info[3] = 33;
    info[1] = gmk_wire_crc8(info + 2, 3);
    assert(!server(&p, &f, info, 5));
    info[3] = 32;
    info[2] = 12;
    info[1] = gmk_wire_crc8(info + 2, 3);
    assert(!server(&p, &f, info, 5));
    info[2] = 18;
    info[1] = gmk_wire_crc8(info + 2, 3);
    assert(server(&p, &f, info, 5));
    assert(!server(&p, &f, request, 20));
    assert(!server(&p, &f, exec, 2));
    assert(calls == 1);
    assert(server(&p, &f, info, 5));
    assert(server(&p, &f, request, 33));
    exec[1] = 17;
    assert(!server(&p, &f, exec, 2));
    assert(calls == 1);
    info[2] = 18;
    info[3] = 31;
    info[4] = 32;
    info[1] = gmk_wire_crc8(info + 2, 3);
    assert(server(&p, &f, info, 5));
    assert(server(&p, &f, request, 32));
    exec[1] = 18;
    assert(!server(&p, &f, exec, 2));
    assert(calls == 1);
    /* USER_SYNC runtime lengths are accepted, ignored, and get zero responses. */
    info[2] = 17;
    info[3] = 7;
    info[4] = 3;
    info[1] = gmk_wire_crc8(info + 2, 3);
    assert(server(&p, &f, info, 5));
    assert(server(&p, &f, request, 8));
    exec[1] = 17;
    assert(server(&p, &f, exec, 2));
    assert(server(&p, &f, &id, 1));
    assert(f.sent == 4 && f.tx[1] == 0 && f.tx[3] == 0);
    struct gmk_debounce d      = {0};
    uint16_t            raw[5] = {1};
    gmk_matrix_debounce(&d, raw, 100);
    assert(d.stable[0] == 0);
    gmk_matrix_debounce(&d, raw, 104);
    assert(d.stable[0] == 0);
    gmk_matrix_debounce(&d, raw, 105);
    assert(d.stable[0] == 1);
    raw[0] = 0;
    gmk_matrix_debounce(&d, raw, 106);
    raw[0] = 1;
    gmk_matrix_debounce(&d, raw, 108);
    assert(d.stable[0] == 1);
    raw[0] = 0;
    gmk_matrix_debounce(&d, raw, 109);
    gmk_matrix_debounce(&d, raw, 114);
    assert(d.stable[0] == 0);
    struct gmk_peer peer = {0};
    gmk_peer_set(&peer, rows, 100);
    assert(gmk_peer_rows(&peer, 199, raw) && raw[0] == 1);
    assert(!gmk_peer_rows(&peer, 200, raw));
    for (unsigned i = 0; i < 5; ++i)
        assert(raw[i] == 0);
    /* A partial master response never leaks its preexisting caller buffer. */
    struct gmk_wire_io io = {&f, read_bytes, write_bytes, clear};
    uint8_t            out[32];
    memset(out, 99, 32);
    f.n = f.pos = 0;
    assert(!gmk_protocol_update(&io, request, out));
    for (unsigned i = 0; i < 32; ++i)
        assert(out[i] == 0);
    puts("wired protocol/debounce/disconnect tests passed");
}
