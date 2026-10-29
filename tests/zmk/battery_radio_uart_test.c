/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "radio.h"
#include "battery_fixture.h"

static struct gmk_radio_core radio;

void gmk_radio_notification(uint8_t command, uint8_t value) {
    gmk_radio_core_notification(&radio, command, value);
}
void gmk_radio_ack(void) {
    gmk_radio_core_ack(&radio);
}
bool gmk_radio_next(uint32_t now, struct gmk_radio_frame *frame) {
    return gmk_radio_core_next(&radio, now, frame);
}
void gmk_radio_sent(uint32_t now, bool success) {
    gmk_radio_core_sent(&radio, now, success);
}
bool gmk_radio_control_busy(void) {
    return radio.waiting || radio.pending_select || radio.pending_pair || radio.inflight == GMK_RADIO_TX_SELECT || radio.inflight == GMK_RADIO_TX_PAIR;
}
static void reset_radio(void) {
    reset_model(true);
    gmk_radio_core_init(&radio);
}
static void ack(void) {
    const uint8_t bytes[] = {0x61, 0x0d, 0x0a};
    feed(bytes, sizeof(bytes));
}
static void expect_bytes(size_t offset, const uint8_t *expected, size_t length) {
    assert(tx_count >= offset + length);
    if (memcmp(tx + offset, expected, length) != 0) {
        fprintf(stderr, "UART fixture mismatch at offset %zu (total %zu):", offset, tx_count);
        for (size_t index = 0; index < tx_count; ++index)
            fprintf(stderr, " %02x", tx[index]);
        fputc('\n', stderr);
    }
    assert(memcmp(tx + offset, expected, length) == 0);
}
static void test_select_then_query_ack_ownership(void) {
    reset_radio();
    assert(gmk_radio_core_select(&radio, GMK_HOST_BT1, false) == 0);
    gmk_battery_poll();
    const uint8_t selection[] = {0xa6, 0x31, 0xd7};
    expect_bytes(0, selection, sizeof(selection));
    assert(tx_count == 3 && snapshot().queries == 0 && radio.waiting);
    gmk_battery_poll();
    assert(tx_count == 3 && snapshot().queries == 0);
    ack();
    const uint8_t query[] = {0xa6, 0x53, 0xf9};
    expect_bytes(3, query, sizeof(query));
    assert(snapshot().queries == 1 && !radio.waiting && state.waiting_ack);
    assert(gmk_radio_core_select(&radio, GMK_HOST_BT2, false) == 0);
    gmk_battery_poll();
    assert(tx_count == 6); /* Battery ACK must precede the next acknowledged control. */
    ack();
    const uint8_t release_keyboard[] = {0xa1, 0, 0, 0, 0, 0, 0, 0, 0, 0xa1};
    const uint8_t release_consumer[] = {0xa3, 0, 0, 0xa3};
    expect_bytes(6, release_keyboard, sizeof(release_keyboard));
    assert(!state.waiting_ack && !radio.waiting);
    gmk_battery_poll();
    expect_bytes(16, release_consumer, sizeof(release_consumer));
    gmk_battery_poll();
    const uint8_t next_selection[] = {0xa6, 0x32, 0xd8};
    expect_bytes(20, next_selection, sizeof(next_selection));
    assert(!state.waiting_ack && radio.waiting && radio.active == GMK_HOST_BT2);
    ack();
    assert(!radio.waiting && snapshot().queries == 1);
}
static void test_control_early_ack_cannot_complete_partial_transmit(void) {
    reset_radio();
    assert(gmk_radio_core_select(&radio, GMK_HOST_BT1, false) == 0);
    tx_allowance = 1;
    gmk_battery_poll();
    assert(tx_count == 1 && !radio.waiting && radio.inflight == GMK_RADIO_TX_SELECT);
    tx_allowance = 1;
    ack();
    assert(tx_count == 2 && !radio.waiting && radio.inflight == GMK_RADIO_TX_SELECT);
    tx_allowance = 1;
    gmk_battery_poll();
    assert(tx_count == 3 && radio.waiting && !radio.inflight && snapshot().queries == 0);
    gmk_battery_poll();
    assert(tx_count == 3 && radio.waiting);
    tx_allowance = UINT32_MAX;
    ack();
    assert(tx_count == 6 && !radio.waiting && state.waiting_ack && snapshot().queries == 1);
}
static void connected(void) {
    reset_radio();
    assert(gmk_radio_core_select(&radio, GMK_HOST_BT1, false) == 0);
    gmk_battery_poll();
    ack();
    ack();
    const uint8_t notification[] = {0x5b, 0x32, 0x8d};
    feed(notification, sizeof(notification));
    assert(radio.connection == GMK_RADIO_CONNECTED);
    for (unsigned tick = 0; tick < 4; ++tick)
        gmk_battery_poll();
    assert(!radio.release && !radio.inflight && !radio.waiting && !state.waiting_ack);
    tx_count = 0;
}
static void test_partial_keyboard_ack_between_frames(void) {
    for (unsigned allowance = 1; allowance <= 2; ++allowance) {
        connected();
        const uint8_t report[8] = {2, 0, 4};
        assert(gmk_radio_core_report(&radio, false, report) == 0);
        const uint8_t expected[] = {0xa1, 2, 0, 4, 0, 0, 0, 0, 0, 0xa7};
        tx_allowance             = allowance;
        gmk_battery_poll();
        assert(tx_count == allowance && radio.inflight == GMK_RADIO_TX_KEYBOARD);
        uint32_t      reports         = radio.reports;
        const uint8_t battery_reply[] = {0x5c, 67, 0x9f};
        tx_allowance                  = allowance;
        feed(battery_reply, sizeof(battery_reply));
        assert(snapshot().valid && snapshot().percentage == 67 && radio.reports == reports);
        tx_allowance = 0;
        ack(); /* Incoming early ACK cannot complete or replace this partial HID frame. */
        assert(tx_count == allowance * 2 && radio.inflight == GMK_RADIO_TX_KEYBOARD && radio.reports == reports);
        while (tx_count < 10) {
            ++battery_test_time;
            tx_allowance = allowance;
            gmk_battery_poll();
            assert(tx_count <= 10); /* ACK cannot be spliced into an immutable report. */
        }
        expect_bytes(0, expected, sizeof(expected));
        assert(radio.reports == reports + 1 && !radio.inflight);
        tx_allowance = UINT32_MAX;
        gmk_battery_poll();
        const uint8_t acknowledgement[] = {0x61, 0x0d, 0x0a};
        expect_bytes(10, acknowledgement, sizeof(acknowledgement));
        assert(tx_count == 13);
    }
}
static void test_partial_timeout_does_not_complete_frame(void) {
    connected();
    const uint8_t report[8] = {0, 0, 4};
    assert(gmk_radio_core_report(&radio, false, report) == 0);
    tx_allowance = 2;
    gmk_battery_poll();
    uint32_t reports = radio.reports;
    assert(tx_count == 2 && radio.inflight == GMK_RADIO_TX_KEYBOARD);
    battery_test_time = 10;
    tx_allowance      = 0;
    gmk_battery_poll();
    assert(radio.reports == reports && !radio.inflight && radio.faulted && radio.errors == 1 && snapshot().errors == 1);
    tx_allowance = UINT32_MAX;
    ++battery_test_time;
    gmk_battery_poll();
    assert(tx_count == 2 && radio.reports == reports);
}
static void test_ten_byte_budget_and_partial_timeout_wrap(void) {
    connected();
    const uint8_t report[8] = {0, 0, 4};
    assert(gmk_radio_core_report(&radio, false, report) == 0);
    assert(gmk_radio_core_report(&radio, false, report) == 0);
    gmk_battery_poll();
    assert(tx_count == 10 && radio.count == 1);
    gmk_battery_poll();
    assert(tx_count == 20 && !radio.count);
    connected();
    assert(gmk_radio_core_report(&radio, false, report) == 0);
    battery_test_time = UINT32_MAX - 5;
    tx_allowance      = 1;
    gmk_battery_poll();
    uint32_t reports  = radio.reports;
    battery_test_time = 3;
    tx_allowance      = 0;
    gmk_battery_poll();
    assert(tx_count == 1 && radio.inflight == GMK_RADIO_TX_KEYBOARD && radio.reports == reports);
    battery_test_time = 4;
    gmk_battery_poll();
    assert(tx_count == 1 && radio.faulted && !radio.inflight && radio.reports == reports);
}
static void test_notifications_obey_checksum_raw_quarantine_and_errors(void) {
    connected();
    const uint8_t invalid[] = {0x5b, 0x33, 0};
    feed(invalid, sizeof(invalid));
    assert(radio.connection == GMK_RADIO_CONNECTED && snapshot().errors == 1);
    const uint8_t raw[] = {0xaf, 0x60, 3, 0x5b, 0x33, 0x8e, 0x2e};
    feed(raw, sizeof(raw));
    assert(radio.connection == GMK_RADIO_CONNECTED);
    const uint8_t header[] = {0xaf, 0x60, 3};
    feed(header, sizeof(header));
    battery_test_time = 11;
    gmk_battery_poll();
    const uint8_t tail[] = {0x5b, 0x33, 0x8e, 0x2e};
    feed(tail, sizeof(tail));
    assert(radio.connection == GMK_RADIO_CONNECTED && snapshot().errors == 2);
    line_error                   = 0x82;
    const uint8_t disconnected[] = {0x5b, 0x33, 0x8e};
    feed(disconnected, sizeof(disconnected));
    assert(radio.connection == GMK_RADIO_CONNECTED && snapshot().errors == 3);
    line_error = 0;
    feed(disconnected, sizeof(disconnected));
    assert(radio.connection == GMK_RADIO_DISCONNECTED);
    const uint8_t fake_connected[] = {0xaf, 0x60, 3, 0x5b, 0x32, 0x8d, 0x2c};
    feed(fake_connected, sizeof(fake_connected));
    assert(radio.connection == GMK_RADIO_DISCONNECTED);
    feed(header, sizeof(header));
    line_error = 0x82;
    gmk_battery_poll();
    line_error = 0;
    feed(fake_connected + 3, sizeof(fake_connected) - 3);
    assert(radio.connection == GMK_RADIO_DISCONNECTED && snapshot().errors == 4);
    const uint8_t connected_status[] = {0x5b, 0x32, 0x8d};
    feed(connected_status, sizeof(connected_status));
    assert(radio.connection == GMK_RADIO_CONNECTED);
}
int main(void) {
    test_select_then_query_ack_ownership();
    test_control_early_ack_cannot_complete_partial_transmit();
    test_partial_keyboard_ack_between_frames();
    test_partial_timeout_does_not_complete_frame();
    test_ten_byte_budget_and_partial_timeout_wrap();
    test_notifications_obey_checksum_raw_quarantine_and_errors();
    puts("Battery/radio UART: real-core serialization, ACK ownership, partial TX, parser quarantine and faults passed");
    return 0;
}
