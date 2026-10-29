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
static void settle(struct gmk_radio_core *radio, uint8_t channel) {
    gmk_radio_core_init(radio);
    assert(gmk_radio_core_select(radio, channel, false) == 0);
    const uint8_t select[] = {0xa6, (uint8_t)(channel == 6 ? 0x30 : 0x30 + channel), (uint8_t)(channel == 6 ? 0xd6 : 0xd6 + channel)};
    expect_frame(radio, 0, select, sizeof(select));
    gmk_radio_core_ack(radio);
    gmk_radio_core_notification(radio, 0x5b, 0x32);
    const uint8_t keyboard[] = {0xa1, 0, 0, 0, 0, 0, 0, 0, 0, 0xa1}, consumer[] = {0xa3, 0, 0, 0xa3};
    expect_frame(radio, 1, keyboard, sizeof(keyboard));
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
    expect_frame(&radio, 3, consumer, sizeof(consumer));
    assert(radio.count == 1 && radio.errors == 1);
    assert(gmk_radio_core_select(&radio, 2, false) == 0);
    expect_frame(&radio, 4, keyboard, sizeof(keyboard));
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
int main(void) {
    selectors_and_non_destructive_pairing();
    fast_taps_and_consumer_transitions();
    releases_precede_switch_and_queue_overflow();
    early_ack_partial_tx_and_bounded_retries();
    status_attribution_and_disconnect();
    failed_partial_frame_requires_explicit_reselection();
    puts("Radio core: exact controls/reports, FIFO/release barriers, finite retries and connection gating passed");
}
