/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "event_link.h"
#include "wired_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t read_reply(struct gmk_event_link *link, uint16_t ack, uint16_t expected, bool data) {
    uint8_t  frame[GMK_EVENT_FRAME], flags;
    uint16_t sequence, rows[5];
    gmk_event_reply(link, ack, frame);
    assert(gmk_event_decode(frame, &sequence, &flags, rows));
    assert(rows[0] == expected && !!(flags & GMK_EVENT_DATA) == data);
    for (unsigned byte = 0; byte < sizeof(frame); ++byte) {
        frame[byte] ^= 1;
        assert(!gmk_event_decode(frame, &sequence, &flags, rows));
        frame[byte] ^= 1;
    }
    return sequence;
}
int main(void) {
    struct gmk_event_link link;
    uint16_t              rows[5] = {0};
    gmk_event_enable(&link, rows, true);
    uint16_t ack = read_reply(&link, 0, 0, true);
    read_reply(&link, 0, 0, true); /* Lost response retains the same head. */
    read_reply(&link, ack, 0, false);
    rows[0] = 4;
    gmk_event_sample(&link, rows, false);
    rows[0] = 0;
    gmk_event_sample(&link, rows, true);
    assert(link.count == 2); /* A whole tap occurs before LEFT is scheduled. */
    uint16_t press = read_reply(&link, ack, 4, true);
    assert(read_reply(&link, ack, 4, true) == press); /* Duplicate old ACK. */
    uint16_t release = read_reply(&link, press, 0, true);
    read_reply(&link, press, 0, true);
    read_reply(&link, release, 0, false);
    assert(!link.count && !gmk_event_notice_due(&link, 10000));
    rows[0] = 1;
    gmk_event_sample(&link, rows, false);
    assert(gmk_event_notice_due(&link, UINT32_MAX - 3));
    gmk_event_notified(&link, UINT32_MAX - 3);
    assert(!gmk_event_notice_due(&link, 5));
    assert(gmk_event_notice_due(&link, 16));
    /* Overflow preserves the outstanding head and ultimately the release. */
    for (unsigned i = 0; i < GMK_EVENT_QUEUE + 10; ++i) {
        rows[0] ^= 1;
        gmk_event_sample(&link, rows, rows[0] == 0);
    }
    rows[0] = 0;
    gmk_event_sample(&link, rows, true);
    assert(link.count == GMK_EVENT_QUEUE && link.overflow);
    while (link.count) {
        uint8_t  frame[GMK_EVENT_FRAME], flags;
        uint16_t sequence, out[5];
        gmk_event_reply(&link, ack, frame);
        assert(gmk_event_decode(frame, &sequence, &flags, out));
        ack = sequence;
        if (!(flags & GMK_EVENT_DATA)) assert(out[0] == 0);
    }
    link.sequence = UINT16_MAX;
    rows[0]       = 2;
    gmk_event_sample(&link, rows, false);
    read_reply(&link, 0, 2, true);
    read_reply(&link, UINT16_MAX, 2, false);
    assert(link.sequence == 1);
    puts("event link: ordered short taps, retransmit, duplicate ACK, CRC, bounded overflow, release and timer/sequence wrap passed");
}
