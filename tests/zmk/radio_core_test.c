/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void expect_frame(struct gmk_radio_core *radio, uint32_t now, const uint8_t *expected, unsigned length) {
    struct gmk_radio_frame frame;
    assert(gmk_radio_core_next(radio, now, &frame));
    assert(frame.length == length && memcmp(frame.bytes, expected, length) == 0);
    assert(!gmk_radio_core_next(radio, now, &frame)); /* Claimed frame is immutable until completed. */
    gmk_radio_core_sent(radio, now, true);
}
static void expect_zero_overflow(struct gmk_radio_core *radio, uint32_t now) {
    const uint8_t overflow[16] = {0xa2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xa2};
    expect_frame(radio, now, overflow, sizeof(overflow));
}
static void settle(struct gmk_radio_core *radio, uint8_t channel) {
    gmk_radio_core_init(radio);
    assert(gmk_radio_core_select(radio, channel, false) == 0);
    const uint8_t select[] = {0xa6, (uint8_t)(channel == 6 ? 0x30 : 0x30 + channel), (uint8_t)(channel == 6 ? 0xd6 : 0xd6 + channel)};
    expect_frame(radio, 0, select, sizeof(select));
    gmk_radio_core_ack(radio);
    gmk_radio_core_notification(radio, 0x5b, 0x32);
    const uint8_t keyboard[] = {0xa1, 0, 0, 0, 0, 0, 0, 0, 0, 0xa1}, consumer[] = {0xa3, 0, 0, 0xa3};
    expect_frame(radio, 1, keyboard, sizeof(keyboard));
    expect_zero_overflow(radio, 1);
    expect_frame(radio, 1, consumer, sizeof(consumer));
    assert(radio->connection == GMK_RADIO_CONNECTED);
}
static void selectors_and_non_destructive_pairing(void) {
    for (uint8_t channel = 0; channel <= 6; ++channel) {
        struct gmk_radio_core radio;
        gmk_radio_core_init(&radio);
        assert(gmk_radio_core_select(&radio, channel, false) == 0);
        uint8_t       command    = channel == 0 ? 0x11 : channel == 6 ? 0x30 : (uint8_t)(0x30 + channel);
        const uint8_t expected[] = {0xa6, command, (uint8_t)(0xa6 + command)};
        expect_frame(&radio, 0, expected, sizeof(expected));
        gmk_radio_core_ack(&radio);
        assert(!radio.pending_pair && radio.active == channel);
        assert(gmk_radio_core_select(&radio, 7, false) == -EINVAL);
        assert(gmk_radio_core_select(&radio, 0, true) == -EINVAL);
    }
    struct gmk_radio_core radio;
    settle(&radio, 1);
    assert(gmk_radio_core_select(&radio, 1, true) == 0);
    const uint8_t keyboard[] = {0xa1, 0, 0, 0, 0, 0, 0, 0, 0, 0xa1}, consumer[] = {0xa3, 0, 0, 0xa3}, pair[] = {0xa6, 0x51, 0xf7};
    expect_frame(&radio, 2, keyboard, sizeof(keyboard));
    expect_zero_overflow(&radio, 2);
    expect_frame(&radio, 2, consumer, sizeof(consumer));
    expect_frame(&radio, 2, pair, sizeof(pair));
    assert(radio.connection == GMK_RADIO_PAIRING);
}
static void fast_taps_and_consumer_transitions(void) {
    struct gmk_radio_core radio;
    settle(&radio, 1);
    const uint8_t down[8] = {2, 0, 4}, up[8] = {0}, volume[8] = {0xe9};
    assert(gmk_radio_core_report(&radio, false, down) == 0);
    assert(gmk_radio_core_report(&radio, false, up) == 0);
    assert(gmk_radio_core_report(&radio, true, volume) == 0);
    assert(gmk_radio_core_report(&radio, true, up) == 0);
    const uint8_t keydown[] = {0xa1, 2, 0, 4, 0, 0, 0, 0, 0, 0xa7}, keyup[] = {0xa1, 0, 0, 0, 0, 0, 0, 0, 0, 0xa1}, consumerdown[] = {0xa3, 0xe9, 0, 0x8c}, consumerup[] = {0xa3, 0, 0, 0xa3};
    expect_frame(&radio, 2, keydown, sizeof(keydown));
    expect_frame(&radio, 2, keyup, sizeof(keyup));
    expect_frame(&radio, 2, consumerdown, sizeof(consumerdown));
    expect_frame(&radio, 2, consumerup, sizeof(consumerup));
}
static void releases_precede_switch_and_queue_overflow(void) {
    struct gmk_radio_core radio;
    settle(&radio, 1);
    const uint8_t down[8] = {1, 0, 5};
    for (unsigned n = 0; n < 9; ++n) {
        int result = gmk_radio_core_report(&radio, false, down);
        assert(result == (n == 8 ? -ENOBUFS : 0));
    }
    const uint8_t keyboard[] = {0xa1, 0, 0, 0, 0, 0, 0, 0, 0, 0xa1}, consumer[] = {0xa3, 0, 0, 0xa3};
    expect_frame(&radio, 3, keyboard, sizeof(keyboard));
    expect_zero_overflow(&radio, 3);
    expect_frame(&radio, 3, consumer, sizeof(consumer));
    assert(radio.count == 1 && radio.errors == 1);
    assert(gmk_radio_core_select(&radio, 2, false) == 0);
    expect_frame(&radio, 4, keyboard, sizeof(keyboard));
    expect_zero_overflow(&radio, 4);
    expect_frame(&radio, 4, consumer, sizeof(consumer));
    const uint8_t select[] = {0xa6, 0x32, 0xd8};
    expect_frame(&radio, 4, select, sizeof(select));
    assert(radio.count == 0 && radio.active == 2);
    assert(gmk_radio_core_report(&radio, false, down) == -ENOTCONN);
}
static void early_ack_partial_tx_and_bounded_retries(void) {
    struct gmk_radio_core  radio;
    struct gmk_radio_frame frame;
    gmk_radio_core_init(&radio);
    assert(gmk_radio_core_select(&radio, 6, false) == 0);
    assert(gmk_radio_core_next(&radio, UINT32_MAX - 5, &frame));
    gmk_radio_core_ack(&radio); /* An ACK before the frame has finished cannot complete it. */
    gmk_radio_core_sent(&radio, UINT32_MAX - 5, true);
    assert(radio.waiting);
    assert(!gmk_radio_core_next(&radio, 3, &frame));
    for (unsigned retry = 0; retry < 50; ++retry) {
        uint32_t      now      = 4 + retry * 10;
        const uint8_t select[] = {0xa6, 0x30, 0xd6};
        expect_frame(&radio, now, select, sizeof(select));
    }
    assert(!gmk_radio_core_next(&radio, 504, &frame));
    assert(!radio.waiting && radio.errors == 1 && radio.connection == GMK_RADIO_UNKNOWN);
    gmk_radio_core_ack(&radio); /* Unsolicited/duplicate ACK cannot revive the channel. */
    assert(!radio.waiting);
    assert(gmk_radio_core_select(&radio, 1, false) == 0);
    assert(gmk_radio_core_next(&radio, 505, &frame));
    gmk_radio_core_sent(&radio, 515, false);
    assert(radio.errors == 2 && radio.connection == GMK_RADIO_UNKNOWN);
}
static void status_attribution_and_disconnect(void) {
    struct gmk_radio_core radio;
    gmk_radio_core_init(&radio);
    assert(gmk_radio_core_select(&radio, 1, false) == 0);
    gmk_radio_core_notification(&radio, 0x5b, 0x32);
    assert(radio.connection == GMK_RADIO_UNKNOWN);
    settle(&radio, 1);
    const uint8_t down[8] = {0, 0, 4};
    assert(gmk_radio_core_report(&radio, false, down) == 0);
    gmk_radio_core_notification(&radio, 0x5b, 0x33);
    assert(radio.connection == GMK_RADIO_DISCONNECTED && radio.count == 0 && radio.clear_requested);
    assert(gmk_radio_core_report(&radio, false, down) == -ENOTCONN);
    gmk_radio_core_notification(&radio, 0x5b, 0x32);
    const uint8_t keyboard[] = {0xa1, 0, 0, 0, 0, 0, 0, 0, 0, 0xa1}, consumer[] = {0xa3, 0, 0, 0xa3};
    expect_frame(&radio, 1, keyboard, sizeof(keyboard));
    expect_zero_overflow(&radio, 1);
    expect_frame(&radio, 1, consumer, sizeof(consumer));
    assert(radio.count == 0);
}
static void failed_partial_frame_requires_explicit_reselection(void) {
    struct gmk_radio_core  radio;
    struct gmk_radio_frame frame;
    gmk_radio_core_init(&radio);
    assert(gmk_radio_core_select(&radio, 1, false) == 0);
    assert(gmk_radio_core_next(&radio, 0, &frame));
    gmk_radio_core_sent(&radio, 10, false);
    for (uint32_t now = 11; now < 100; ++now)
        assert(!gmk_radio_core_next(&radio, now, &frame));
    assert(gmk_radio_core_select(&radio, 1, false) == 0);
    assert(gmk_radio_core_next(&radio, 100, &frame));
}
static void nkro_snapshots_are_coherent_and_preserve_fast_taps(void) {
    struct gmk_radio_core radio;
    settle(&radio, 1);
    uint8_t keyboard[8]  = {2, 0, 4, 5, 6, 7, 8, 9};
    uint8_t overflow[14] = {0, 4};
    assert(gmk_radio_core_nkro(&radio, keyboard, overflow) == 0);
    memset(keyboard, 0, sizeof(keyboard));
    memset(overflow, 0, sizeof(overflow));
    assert(gmk_radio_core_nkro(&radio, keyboard, overflow) == 0);
    const uint8_t pressed[]  = {0xa1, 2, 0, 4, 5, 6, 7, 8, 9, 0xca};
    const uint8_t extra[]    = {0xa2, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xa6};
    const uint8_t released[] = {0xa1, 0, 0, 0, 0, 0, 0, 0, 0, 0xa1};
    expect_frame(&radio, 2, pressed, sizeof(pressed));
    expect_frame(&radio, 2, extra, sizeof(extra));
    expect_frame(&radio, 2, released, sizeof(released));
    expect_zero_overflow(&radio, 2);
    assert(!radio.count);
}
static void interrupted_nkro_releases_both_formats_before_host_controls(void) {
    for (unsigned action = 0; action < 4; ++action) {
        struct gmk_radio_core  radio;
        struct gmk_radio_frame frame;
        settle(&radio, 1);
        const uint8_t keyboard[8]  = {0, 0, 4};
        const uint8_t overflow[14] = {0, 4};
        assert(gmk_radio_core_nkro(&radio, keyboard, overflow) == 0);
        assert(gmk_radio_core_next(&radio, 2, &frame) && frame.bytes[0] == 0xa1);
        if (action == 0)
            gmk_radio_core_release(&radio, false);
        else if (action == 1)
            assert(gmk_radio_core_select(&radio, 2, false) == 0);
        else if (action == 2)
            assert(gmk_radio_core_select(&radio, 1, true) == 0);
        else {
            for (unsigned queued = 0; queued < 8; ++queued)
                assert(gmk_radio_core_nkro(&radio, keyboard, overflow) == (queued == 7 ? -ENOBUFS : 0));
        }
        gmk_radio_core_sent(&radio, 2, true);
        const uint8_t release[] = {0xa1, 0, 0, 0, 0, 0, 0, 0, 0, 0xa1};
        expect_frame(&radio, 3, release, sizeof(release));
        expect_zero_overflow(&radio, 3);
        if (action) {
            const uint8_t consumer[] = {0xa3, 0, 0, 0xa3};
            expect_frame(&radio, 3, consumer, sizeof(consumer));
        }
        if (action == 1 || action == 2) {
            const uint8_t control[] = {0xa6, action == 1 ? 0x32 : 0x51, action == 1 ? 0xd8 : 0xf7};
            expect_frame(&radio, 3, control, sizeof(control));
        } else if (action == 0)
            assert(!gmk_radio_core_next(&radio, 3, &frame));
        else {
            assert(gmk_radio_core_next(&radio, 3, &frame) && frame.bytes[0] == 0xa1 && frame.bytes[3] == 4);
            gmk_radio_core_sent(&radio, 3, true);
            const uint8_t extra[] = {0xa2, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xa6};
            expect_frame(&radio, 3, extra, sizeof(extra));
        }
    }
}
static void failed_overflow_requires_clears_before_reselection(void) {
    struct gmk_radio_core  radio;
    struct gmk_radio_frame frame;
    settle(&radio, 1);
    const uint8_t keyboard[8]  = {0, 0, 4};
    const uint8_t overflow[14] = {0, 4};
    assert(gmk_radio_core_nkro(&radio, keyboard, overflow) == 0);
    assert(gmk_radio_core_next(&radio, 2, &frame) && frame.bytes[0] == 0xa1);
    gmk_radio_core_sent(&radio, 2, true);
    assert(gmk_radio_core_next(&radio, 2, &frame) && frame.bytes[0] == 0xa2);
    gmk_radio_core_sent(&radio, 2, false);
    assert(radio.faulted && !gmk_radio_core_next(&radio, 3, &frame));
    assert(gmk_radio_core_select(&radio, 2, false) == 0);
    const uint8_t release[]  = {0xa1, 0, 0, 0, 0, 0, 0, 0, 0, 0xa1};
    const uint8_t consumer[] = {0xa3, 0, 0, 0xa3};
    const uint8_t control[]  = {0xa6, 0x32, 0xd8};
    expect_frame(&radio, 3, release, sizeof(release));
    expect_zero_overflow(&radio, 3);
    expect_frame(&radio, 3, consumer, sizeof(consumer));
    expect_frame(&radio, 3, control, sizeof(control));
}
int main(void) {
    nkro_snapshots_are_coherent_and_preserve_fast_taps();
    interrupted_nkro_releases_both_formats_before_host_controls();
    failed_overflow_requires_clears_before_reselection();
    selectors_and_non_destructive_pairing();
    fast_taps_and_consumer_transitions();
    releases_precede_switch_and_queue_overflow();
    early_ack_partial_tx_and_bounded_retries();
    status_attribution_and_disconnect();
    failed_partial_frame_requires_explicit_reselection();
    puts("Radio core: exact controls/reports, FIFO/release barriers, finite retries and connection gating passed");
}
