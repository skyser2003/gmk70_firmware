/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lighting_frame.h"
#include "wired_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void frame_roundtrip(void) {
    struct gmk_light_frame tx = {0}, rx = {0};
    uint8_t                rgb[123], other[123], packets[11][16];
    for (unsigned i = 0; i < sizeof(rgb); ++i)
        rgb[i] = (uint8_t)(i * 7);
    memset(other, 0x5a, sizeof(other));
    assert(gmk_light_queue(&tx, rgb));
    for (unsigned i = 0; i < 11; ++i) {
        assert(gmk_light_next(&tx, packets[i]));
        gmk_light_sent(&tx, packets[i], true);
    }
    assert(!gmk_light_next(&tx, packets[0]));
    assert(!gmk_light_claim(&rx));
    for (unsigned i = 0; i < 10; ++i) {
        assert(gmk_light_accept(&rx, packets[i]));
        assert(!gmk_light_claim(&rx));
    }
    uint8_t corrupt[16];
    memcpy(corrupt, packets[10], 16);
    corrupt[5] ^= 1;
    assert(!gmk_light_accept(&rx, corrupt));
    assert(!gmk_light_claim(&rx));
    assert(gmk_light_accept(&rx, packets[10]));
    assert(gmk_light_claim(&rx));
    assert(!memcmp(rx.rgb, rgb, sizeof(rgb)));
    assert(!gmk_light_accept(&rx, packets[0])); /* Claimed buffer is immutable. */
    gmk_light_release(&rx);
    assert(!gmk_light_claim(&rx));
    /* Reorder within one frame, duplicate a chunk, and reject other frames. */
    assert(gmk_light_accept(&rx, packets[0]));
    for (int i = 10; i > 0; --i)
        assert(gmk_light_accept(&rx, packets[i]));
    assert(gmk_light_claim(&rx));
    assert(!memcmp(rx.rgb, rgb, sizeof(rgb)));
    gmk_light_release(&rx);
    assert(gmk_light_queue(&tx, other));
    uint8_t next[16];
    assert(gmk_light_next(&tx, next));
    assert(gmk_light_accept(&rx, next));
    assert(!gmk_light_accept(&rx, packets[1]));
    assert(!gmk_light_claim(&rx));
    gmk_light_sent(&tx, next, false);
    assert(!gmk_light_next(&tx, next));
    assert(gmk_light_queue(&tx, rgb));
    for (unsigned i = 0; i < 11; ++i) {
        assert(gmk_light_next(&tx, next));
        assert(gmk_light_accept(&rx, next));
        gmk_light_sent(&tx, next, true);
    }
    assert(gmk_light_claim(&rx));
    assert(!memcmp(rx.rgb, rgb, sizeof(rgb)));
    gmk_light_release(&rx);
    /* No stale state survives mode changes, and sequence wrap is harmless. */
    for (unsigned f = 0; f < 260; ++f) {
        assert(gmk_light_queue(&tx, rgb));
        for (unsigned i = 0; i < 11; ++i) {
            assert(gmk_light_next(&tx, next));
            assert(gmk_light_accept(&rx, next));
            gmk_light_sent(&tx, next, true);
        }
        assert(gmk_light_claim(&rx));
        gmk_light_release(&rx);
    }
    gmk_light_reset(&rx);
    assert(!gmk_light_accept(&rx, packets[4]));
    assert(!gmk_light_claim(&rx));
    for (unsigned i = 0; i < 16; ++i) {
        memcpy(corrupt, packets[0], 16);
        corrupt[i] ^= 0x80;
        assert(!gmk_light_accept(&rx, corrupt));
    }
    memcpy(corrupt, packets[0], 16);
    corrupt[2]  = 11;
    corrupt[15] = gmk_wire_crc8(corrupt, 15);
    assert(!gmk_light_accept(&rx, corrupt));
}
static void latest_wins(void) {
    struct gmk_light_frame tx = {0}, rx      = {0};
    uint8_t                on[123], off[123] = {0}, packet[16];
    memset(on, 50, sizeof(on));
    assert(gmk_light_queue(&tx, on));
    assert(gmk_light_next(&tx, packet));
    uint8_t stale[16];
    memcpy(stale, packet, sizeof(stale));
    assert(gmk_light_queue(&tx, off)); /* Off has only one upstream callback. */
    gmk_light_sent(&tx, stale, true);  /* Result of a superseded UART send. */
    assert(gmk_light_next(&tx, packet) && packet[2] == 0);
    for (unsigned i = 0; i < 11; ++i) {
        assert(gmk_light_next(&tx, packet));
        assert(gmk_light_accept(&rx, packet));
        gmk_light_sent(&tx, packet, true);
    }
    assert(gmk_light_claim(&rx) && !memcmp(rx.rgb, off, sizeof(off)));
    gmk_light_release(&rx);
    assert(gmk_light_refresh(&tx)); /* Repaint after a lost packet/right reset. */
    assert(gmk_light_next(&tx, packet) && packet[2] == 0);
    gmk_light_sent(&tx, packet, false);
    assert(gmk_light_refresh(&tx));
    gmk_light_reset(&tx);
    assert(!gmk_light_refresh(&tx));
}
int main(void) {
    frame_roundtrip();
    latest_wins();
    puts("Lighting frames: full-frame commit, CRC, loss, reordering, busy ownership and sequence wrap passed");
}
