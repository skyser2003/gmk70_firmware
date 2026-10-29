/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "battery_fixture.h"
static void assert_only_approved_messages(void) {
    assert(tx_count % 3 == 0);
    for (size_t i = 0; i < tx_count; i += 3) {
        const uint8_t query[3] = {0xa6, 0x53, 0xf9}, ack[3] = {0x61, 0x0d, 0x0a};
        assert(memcmp(tx + i, query, 3) == 0 || memcmp(tx + i, ack, 3) == 0);
    }
}
static void test_role_guard_and_pin_isolation(void) {
    reset_model(false);
    gmk_battery_poll();
    assert(!snapshot().valid && mmio_reads == 0 && mmio_writes == 0);
    reset_model(true);
    assert((rcc_registers[0x4c / 4] & 8U) != 0);
    assert((rcc_registers[0x94 / 4] & 8U) == 0);
    assert(((gpio_registers[0][0] >> 14) & 3) == 0);
    assert(((gpio_registers[0][0x0c / 4] >> 14) & 3) == 0);
    assert((gpio_registers[0][0] >> 30) == 0);
    assert((gpio_registers[0][0x0c / 4] >> 30) == 1);
    assert(((gpio_registers[1][0] >> 20) & 15) == 10);
    assert(((gpio_registers[1][0x24 / 4] >> 8) & 255) == 0x77);
    assert(((gpio_registers[0][0] >> 12) & 3) == ((0xa5a5a5a5U >> 12) & 3));
    assert(((gpio_registers[1][0] >> 24) & 3) == ((0xa5a5a5a5U >> 24) & 3));
    assert(uart_registers[0x0c / 4] == 3 && uart_registers[4 / 4] == 0);
    assert(uart_registers[0xc0 / 4] == 1 && uart_registers[0] == 52);
}
static void test_query_and_single_retry(void) {
    reset_model(true);
    gmk_battery_poll();
    assert(snapshot().queries == 1 && tx_count == 3);
    battery_test_time = 9;
    gmk_battery_poll();
    assert(tx_count == 3);
    battery_test_time = 10;
    gmk_battery_poll();
    assert(snapshot().queries == 2 && tx_count == 6);
    battery_test_time = 2999;
    gmk_battery_poll();
    assert(tx_count == 6 && snapshot().errors == 1);
    battery_test_time = 3000;
    gmk_battery_poll();
    assert(tx_count == 9);
    const uint8_t ack[] = {0x61, 0x0d, 0x0a};
    feed(ack, sizeof(ack));
    battery_test_time = 3010;
    gmk_battery_poll();
    assert(tx_count == 9 && !snapshot().valid);
    assert_only_approved_messages();
}
static void test_percentage_freshness_and_gpio_cache(void) {
    reset_model(true);
    gpio_registers[0][0x10 / 4] = (1U << 7) | (1U << 15);
    battery_test_time           = 100;
    const uint8_t reply[]       = {0x5c, 67, 0x9f};
    feed(reply, sizeof(reply));
    struct gmk_battery_status status = snapshot();
    assert(status.valid && status.percentage == 67 && status.replies == 1 && status.age_ms == 0);
    assert(status.cable && status.full);
    gpio_registers[0][0x10 / 4] = 0;
    assert(snapshot().full); /* Callbacks read only the published snapshot. */
    gmk_battery_poll();
    status = snapshot();
    assert(!status.cable && !status.full);
    battery_test_time = 10100;
    assert(snapshot().valid && snapshot().age_ms == 10000);
    battery_test_time = 10101;
    status            = snapshot();
    assert(!status.valid && status.percentage == 67 && status.age_ms == 10001);
    assert_only_approved_messages();
}
static void test_rejects_bad_percentage_and_checksum(void) {
    reset_model(true);
    const uint8_t corrupt[] = {0x5c, 67, 0x9e};
    feed(corrupt, sizeof(corrupt));
    const uint8_t invalid[] = {0x5c, 101, 0xc1};
    feed(invalid, sizeof(invalid));
    assert(!snapshot().valid && snapshot().replies == 0 && snapshot().errors == 2);
    const uint8_t zero[] = {0x5c, 0, 0x5c};
    feed(zero, sizeof(zero));
    assert(snapshot().valid && snapshot().percentage == 0);
    const uint8_t full[] = {0x5c, 100, 0xc0};
    feed(full, sizeof(full));
    assert(snapshot().valid && snapshot().percentage == 100);
    assert_only_approved_messages();
}
static void test_raw_payload_does_not_become_battery_reply(void) {
    reset_model(true);
    uint8_t raw[36] = {0xaf, 0x60, 32, 0x5c, 67, 0x9f};
    /* Header/payload sum: AF+60+20+5C+43+9F = 26D. */
    raw[35] = 0x6d;
    feed(raw, sizeof(raw));
    assert(!snapshot().valid && snapshot().replies == 0 && snapshot().errors == 0);
    uint8_t oversized[259] = {0xaf, 0x60, 255, 0x5c, 67, 0x9f};
    feed(oversized, sizeof(oversized));
    while (rx_cursor < rx_count) {
        ++battery_test_time;
        gmk_battery_poll();
    }
    assert(!snapshot().valid && snapshot().replies == 0 && snapshot().errors >= 1);
    assert_only_approved_messages();
}
static void test_timeout_line_errors_and_work_budget(void) {
    reset_model(true);
    const uint8_t partial[] = {0x5c, 99};
    feed(partial, sizeof(partial));
    battery_test_time = 11;
    gmk_battery_poll();
    const uint8_t valid[] = {0x5c, 30, 0x7a};
    feed(valid, sizeof(valid));
    assert(snapshot().valid && snapshot().percentage == 30 && snapshot().errors >= 1);
    line_error = 0x82; /* RX FIFO error and overrun. */
    feed(valid, sizeof(valid));
    assert(snapshot().replies == 1 && snapshot().errors >= 2);
    line_error      = 0;
    endless_rx      = true;
    unsigned before = receive_reads;
    gmk_battery_poll();
    assert(receive_reads - before == 64);
    endless_rx = false;
    assert_only_approved_messages();
}
static void test_expired_raw_tail_is_discarded(void) {
    reset_model(true);
    const uint8_t partial_raw[] = {0xaf, 0x60, 32};
    feed(partial_raw, sizeof(partial_raw));
    battery_test_time    = 11;
    const uint8_t tail[] = {0x5c, 67, 0x9f};
    feed(tail, sizeof(tail));
    assert(!snapshot().valid && snapshot().replies == 0 && snapshot().errors >= 1);
}
static void test_expired_raw_frame_keeps_its_remaining_byte_budget(void) {
    reset_model(true);
    gmk_battery_poll();
    const uint8_t ack[] = {0x61, 0x0d, 0x0a};
    feed(ack, sizeof(ack)); /* Keep query timeouts separate from parser errors. */
    const uint8_t header[] = {0xaf, 0x60, 32};
    feed(header, sizeof(header));
    battery_test_time = 11;
    gmk_battery_poll(); /* The delayed tail arrives after this empty poll. */
    battery_test_time              = 12;
    const uint8_t apparent_reply[] = {0x5c, 67, 0x9f};
    feed(apparent_reply, sizeof(apparent_reply));
    assert(!snapshot().valid && snapshot().replies == 0 && snapshot().errors == 1);
    battery_test_time = 100;
    gmk_battery_poll();
    gmk_battery_poll();
    assert(snapshot().errors == 1 && tx_count == 3);
    uint8_t remainder[30] = {0};
    remainder[29]         = 0x6d; /* Complete checksum-valid raw frame, now expired. */
    feed(remainder, sizeof(remainder));
    assert(!snapshot().valid && tx_count == 3); /* No ACK for expired raw. */
    feed(apparent_reply, sizeof(apparent_reply));
    assert(snapshot().valid && snapshot().percentage == 67 && snapshot().replies == 1 && tx_count == 6);
    assert_only_approved_messages();
}
static void test_expired_partial_raw_headers_use_a_bounded_quarantine(void) {
    for (size_t header_size = 1; header_size <= 2; ++header_size) {
        reset_model(true);
        gmk_battery_poll();
        const uint8_t ack[] = {0x61, 0x0d, 0x0a};
        feed(ack, sizeof(ack));
        const uint8_t header[] = {0xaf, 0x60};
        feed(header, header_size);
        battery_test_time = 11;
        gmk_battery_poll();
        battery_test_time              = 12;
        const uint8_t apparent_reply[] = {0x5c, 67, 0x9f};
        feed(apparent_reply, sizeof(apparent_reply));
        assert(!snapshot().valid && snapshot().errors == 1);
        uint8_t remainder[255] = {0};
        feed(remainder, 259 - header_size - sizeof(apparent_reply));
        while (rx_cursor < rx_count) {
            ++battery_test_time;
            gmk_battery_poll();
        }
        assert(!snapshot().valid && snapshot().replies == 0 && snapshot().errors == 1 && tx_count == 3);
        feed(apparent_reply, sizeof(apparent_reply));
        assert(snapshot().valid && snapshot().replies == 1 && snapshot().percentage == 67);
        assert_only_approved_messages();
    }
}
static void test_uart_fault_does_not_release_a_known_raw_tail(void) {
    reset_model(true);
    gmk_battery_poll();
    const uint8_t ack[] = {0x61, 0x0d, 0x0a};
    feed(ack, sizeof(ack));
    const uint8_t header[] = {0xaf, 0x60, 3};
    feed(header, sizeof(header));
    line_error = 0x82;
    gmk_battery_poll();
    line_error           = 0;
    const uint8_t tail[] = {0x5c, 67, 0x9f, 0x50};
    feed(tail, sizeof(tail));
    assert(!snapshot().valid && snapshot().replies == 0 && snapshot().errors == 1 && tx_count == 3);
    const uint8_t reply[] = {0x5c, 67, 0x9f};
    feed(reply, sizeof(reply));
    assert(snapshot().valid && snapshot().replies == 1 && tx_count == 6);
    assert_only_approved_messages();
}
static void test_oversized_raw_frame_keeps_quarantine_across_delays(void) {
    reset_model(true);
    gmk_battery_poll();
    const uint8_t ack[] = {0x61, 0x0d, 0x0a};
    feed(ack, sizeof(ack));
    const uint8_t header[] = {0xaf, 0x60, 255};
    feed(header, sizeof(header));
    assert(snapshot().errors == 1);
    battery_test_time = 11;
    gmk_battery_poll();
    gmk_battery_poll();
    battery_test_time              = 12;
    const uint8_t apparent_reply[] = {0x5c, 67, 0x9f};
    feed(apparent_reply, sizeof(apparent_reply));
    assert(!snapshot().valid && snapshot().errors == 1 && tx_count == 3);
    uint8_t remainder[253] = {0};
    feed(remainder, sizeof(remainder));
    while (rx_cursor < rx_count) {
        ++battery_test_time;
        gmk_battery_poll();
    }
    assert(!snapshot().valid && snapshot().errors == 1 && tx_count == 3);
    feed(apparent_reply, sizeof(apparent_reply));
    assert(snapshot().valid && snapshot().replies == 1 && snapshot().percentage == 67);
    assert_only_approved_messages();
}
static void test_other_statuses_are_acked_without_battery_updates(void) {
    reset_model(true);
    const uint8_t frames[] = {0x5a, 15, 0x69, 0x5b, 50, 0x8d, 0x5d, 1, 0x5e, 0x60, 1, 0x61, 0xaf, 0x60, 0, 0x0f};
    feed(frames, sizeof(frames));
    for (unsigned poll = 0; poll < 6; ++poll)
        gmk_battery_poll();
    assert(!snapshot().valid && snapshot().errors == 0 && snapshot().queries == 1);
    assert(tx_count == 18); /* Five ACKs and one query. */
    assert_only_approved_messages();
}
static void test_partial_tx_and_stopped_fifo_are_bounded(void) {
    reset_model(true);
    tx_ready = false;
    gmk_battery_poll();
    assert(tx_count == 0 && snapshot().queries == 0);
    battery_test_time = 11;
    gmk_battery_poll();
    assert(tx_count == 0 && snapshot().errors >= 1);
    battery_test_time = 3000;
    tx_ready          = true;
    tx_allowance      = 1;
    gmk_battery_poll();
    assert(tx_count == 1 && snapshot().queries == 0);
    ++battery_test_time;
    tx_allowance = 2;
    gmk_battery_poll();
    assert(tx_count == 3 && snapshot().queries == 1);
    assert_only_approved_messages();
}
static void test_sensor_is_percentage_only_and_cache_only(void) {
    reset_model(true);
    struct sensor_value value = {123, 456};
    assert(battery_sensor_init(NULL) == 0);
    unsigned reads = mmio_reads, writes = mmio_writes;
    assert(battery_sample_fetch(NULL, SENSOR_CHAN_ALL) == -ENODATA);
    assert(battery_channel_get(NULL, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &value) == -ENODATA);
    assert(value.val1 == 123 && value.val2 == 456);
    assert(battery_channel_get(NULL, SENSOR_CHAN_GAUGE_VOLTAGE, &value) == -ENOTSUP);
    assert(battery_sample_fetch(NULL, SENSOR_CHAN_GAUGE_VOLTAGE) == -ENOTSUP);
    assert(mmio_reads == reads && mmio_writes == writes);
    battery_test_time     = UINT32_MAX - 10;
    const uint8_t reply[] = {0x5c, 99, 0xbf};
    feed(reply, sizeof(reply));
    battery_test_time = 10;
    assert(snapshot().valid && snapshot().age_ms == 21);
    reads  = mmio_reads;
    writes = mmio_writes;
    assert(battery_sample_fetch(NULL, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE) == 0);
    assert(battery_channel_get(NULL, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &value) == 0);
    assert(value.val1 == 99 && value.val2 == 0);
    assert(mmio_reads == reads && mmio_writes == writes);
    battery_test_time = 10000;
    assert(battery_sample_fetch(NULL, SENSOR_CHAN_ALL) == -ENODATA);
    left = false;
    assert(battery_sensor_init(NULL) == -ENODEV);
}
static void test_stale_sample_never_revives_without_another_reply(void) {
    reset_model(true);
    const uint8_t reply[] = {0x5c, 67, 0x9f};
    feed(reply, sizeof(reply));
    battery_test_time = 10001;
    gmk_battery_poll();
    assert(!snapshot().valid && snapshot().age_ms == 10001 && snapshot().percentage == 67);
    /* Simulate the timestamp returning to the same value after a full wrap. */
    battery_test_time = 0;
    assert(!snapshot().valid && snapshot().replies == 1);
    feed(reply, sizeof(reply));
    assert(snapshot().valid && snapshot().replies == 2);
}
int main(void) {
    assert(!snapshot().valid && mmio_reads == 0 && mmio_writes == 0);
    test_role_guard_and_pin_isolation();
    test_query_and_single_retry();
    test_percentage_freshness_and_gpio_cache();
    test_rejects_bad_percentage_and_checksum();
    test_raw_payload_does_not_become_battery_reply();
    test_timeout_line_errors_and_work_budget();
    test_expired_raw_tail_is_discarded();
    test_expired_raw_frame_keeps_its_remaining_byte_budget();
    test_expired_partial_raw_headers_use_a_bounded_quarantine();
    test_uart_fault_does_not_release_a_known_raw_tail();
    test_oversized_raw_frame_keeps_quarantine_across_delays();
    test_other_statuses_are_acked_without_battery_updates();
    test_partial_tx_and_stopped_fifo_are_bounded();
    test_sensor_is_percentage_only_and_cache_only();
    test_stale_sample_never_revives_without_another_reply();
    puts("Battery: bounded UART3, stock framing, validity, GPIO isolation and cached sensor passed");
    return 0;
}
