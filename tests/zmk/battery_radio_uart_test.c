/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "radio.h"
#include "battery_fixture.h"
#ifdef CONFIG_GMK70_EVENT_IDLE
#    include "battery_irq_fixture.h"
#endif

static struct gmk_radio_core radio;
bool                         gmk_radio_selected(void) {
    return radio.selected != GMK_HOST_USB;
}
void gmk_radio_charger_state(bool cable, bool full) {
    gmk_radio_core_charger(&radio, cable, full);
}

void gmk_radio_notification(uint8_t command, uint8_t value) {
    gmk_radio_core_notification(&radio, command, value);
}
void gmk_radio_ack(void) {
    gmk_radio_core_ack(&radio);
}
enum gmk_radio_turn gmk_radio_next(uint32_t now, struct gmk_radio_frame *frame, bool battery_due) {
    return gmk_radio_core_next(&radio, now, frame, battery_due);
}
void gmk_radio_sent(uint32_t now, bool success) {
    gmk_radio_core_sent(&radio, now, success);
}
bool gmk_radio_control_busy(void) {
    return gmk_radio_core_control_busy(&radio);
}
static void reset_radio(void) {
    reset_model(true);
    gmk_radio_core_init(&radio);
}
static void ack(void) {
    const uint8_t bytes[] = {0x61, 0x0d, 0x0a};
    feed(bytes, sizeof(bytes));
}
static void test_charging_updates_the_module_without_restarting(void) {
    reset_radio();
    assert(gmk_radio_core_select(&radio, GMK_HOST_USB, false) == 0);
    gmk_battery_request(); /* Wired level display remains explicitly refreshable. */
    gpio_registers[0][0x10 / 4] = 1U << 7;
    gmk_battery_poll();
    const uint8_t usb[] = {0xa6, 0x11, 0xb7};
    assert(tx_count == 3 && !memcmp(tx, usb, sizeof(usb)));
    ack();
    const uint8_t charging[] = {0xa6, 0x64, 0x0a};
    assert(tx_count == 6 && !memcmp(tx + 3, charging, sizeof(charging)));
    ack(); /* The charge control owns its ACK before a battery query starts. */
    const uint8_t query[] = {0xa6, 0x53, 0xf9};
    assert(tx_count == 9 && !memcmp(tx + 6, query, sizeof(query)));
    ack();
    ack(); /* A duplicate ACK cannot start another wired query. */
    const uint8_t low[] = {0x5c, 3, 0x5f};
    feed(low, sizeof(low));
    assert(snapshot().valid && snapshot().percentage == 3);
    battery_test_time = 1000;
    size_t before     = tx_count;
    gmk_battery_poll();
    assert(tx_count == before + 3 && !memcmp(tx + before, charging, sizeof(charging)));
    ack();
    const uint8_t increased[] = {0x5c, 67, 0x9f};
    feed(increased, sizeof(increased));
    struct sensor_value value;
    assert(snapshot().percentage == 67 && !battery_channel_get(NULL, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &value));
    assert(value.val1 == 67 && value.val2 == 0);
    gpio_registers[0][0x10 / 4] |= 1U << 15;
    before = tx_count;
    gmk_battery_poll();
    const uint8_t full[] = {0xa6, 0x66, 0x0c};
    assert(tx_count == before + 3 && !memcmp(tx + before, full, sizeof(full)));
    ack();
    gpio_registers[0][0x10 / 4] = 0;
    before                      = tx_count;
    gmk_battery_poll();
    const uint8_t stopped[] = {0xa6, 0x65, 0x0b};
    assert(tx_count == before + 3 && !memcmp(tx + before, stopped, sizeof(stopped)));
    ack();
    assert(!snapshot().cable && !snapshot().full && snapshot().percentage == 67);
}
static void expect_bytes(size_t offset, const uint8_t *expected, size_t length) {
    if (tx_count < offset + length) fprintf(stderr, "UART fixture is short at offset %zu: expected %zu bytes, have %zu\n", offset, length, tx_count);
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
    const uint8_t stopped[] = {0xa6, 0x65, 0x0b};
    expect_bytes(3, stopped, sizeof(stopped));
    assert(tx_count == 6 && radio.waiting && !state.waiting_ack);
    ack();
    const uint8_t query[] = {0xa6, 0x53, 0xf9};
    expect_bytes(6, query, sizeof(query));
    assert(snapshot().queries == 1 && !radio.waiting && state.waiting_ack);
    assert(gmk_radio_core_select(&radio, GMK_HOST_BT2, false) == 0);
    gmk_battery_poll();
    assert(tx_count == 9); /* Battery ACK must precede the next acknowledged control. */
    ack();
    const uint8_t release_keyboard[]   = {0xa1, 0, 0, 0, 0, 0, 0, 0, 0, 0xa1};
    const uint8_t release_consumer[]   = {0xa3, 0, 0, 0xa3};
    const uint8_t release_overflow[16] = {0xa2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xa2};
    expect_bytes(9, release_keyboard, sizeof(release_keyboard));
    assert(!state.waiting_ack && !radio.waiting);
    gmk_battery_poll();
    expect_bytes(19, release_overflow, sizeof(release_overflow));
    gmk_battery_poll();
    expect_bytes(35, release_consumer, sizeof(release_consumer));
    gmk_battery_poll();
    const uint8_t next_selection[] = {0xa6, 0x32, 0xd8};
    expect_bytes(39, next_selection, sizeof(next_selection));
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
    assert(tx_count == 6 && radio.waiting && !state.waiting_ack && !snapshot().queries);
    ack();
    assert(tx_count == 9 && !radio.waiting && state.waiting_ack && snapshot().queries == 1);
}
static void connected_at(uint32_t now) {
    reset_radio();
    battery_test_time = now;
    assert(gmk_radio_core_select(&radio, GMK_HOST_BT1, false) == 0);
    gmk_battery_poll();
    ack();
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
static void connected(void) {
    connected_at(0);
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
    assert(radio.reports == reports && radio.inflight == GMK_RADIO_TX_RELEASE_KEYBOARD && radio.faulted && radio.errors == 1 && snapshot().errors == 1);
    tx_allowance = UINT32_MAX;
    ++battery_test_time;
    gmk_battery_poll();
    const uint8_t release[] = {0xa1, 0, 0, 0, 0, 0, 0, 0, 0, 0xa1};
    expect_bytes(2, release, sizeof(release));
    assert(tx_count == 12 && radio.reports == reports + 1);
    assert(gmk_radio_core_report(&radio, false, report) == -ENOTCONN);
}
static void test_nkro_overflow_is_immutable_during_partial_uart_transmit(void) {
    for (unsigned allowance = 1; allowance <= 2; ++allowance) {
        connected();
        const uint8_t keyboard[8]  = {2, 0, 4};
        uint8_t       overflow[14] = {0, 4};
        assert(gmk_radio_core_nkro(&radio, keyboard, overflow) == 0);
        memset(overflow, 0, sizeof(overflow));
        tx_allowance = 32; /* The stock UART FIFO has room for the full A1 frame. */
        gmk_battery_poll();
        assert(tx_count == 10 && !radio.inflight && radio.count == 1);
        tx_allowance = allowance;
        gmk_battery_poll();
        assert(tx_count == 10 + allowance && radio.inflight == GMK_RADIO_TX_NKRO);
        const uint8_t reply[] = {0x5c, 67, 0x9f};
        tx_allowance          = 0;
        feed(reply, sizeof(reply));
        assert(snapshot().valid && state.acknowledgements && tx_count == 10 + allowance);
        while (state.transmitting == BATTERY_TX_RADIO) {
            /* Several FIFO refills may occur inside one millisecond. Keep the
             * complete frame inside the existing ten-millisecond deadline. */
            tx_allowance = allowance;
            gmk_battery_poll();
            assert(tx_count <= 26);
        }
        const uint8_t expected[16] = {0xa2, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xa6};
        expect_bytes(10, expected, sizeof(expected));
        assert(tx_count == 26 && !radio.count);
        tx_allowance = 32;
        gmk_battery_poll();
        const uint8_t acknowledgement[] = {0x61, 0x0d, 0x0a};
        expect_bytes(26, acknowledgement, sizeof(acknowledgement));
        assert(tx_count == 29);
    }
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
    connected_at(UINT32_MAX - 5);
    assert(gmk_radio_core_report(&radio, false, report) == 0);
    tx_allowance = 1;
    gmk_battery_poll();
    uint32_t reports  = radio.reports;
    battery_test_time = 3;
    tx_allowance      = 0;
    gmk_battery_poll();
    assert(tx_count == 1 && radio.inflight == GMK_RADIO_TX_KEYBOARD && radio.reports == reports);
    battery_test_time = 4;
    gmk_battery_poll();
    assert(tx_count == 1 && radio.faulted && radio.inflight == GMK_RADIO_TX_RELEASE_KEYBOARD && radio.reports == reports);
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
#ifdef CONFIG_GMK70_EVENT_IDLE
    fake_uart_isr(NULL);
#endif
    gmk_battery_poll();
    line_error = 0;
    feed(fake_connected + 3, sizeof(fake_connected) - 3);
    assert(radio.connection == GMK_RADIO_DISCONNECTED && snapshot().errors == 4);
    const uint8_t connected_status[] = {0x5b, 0x32, 0x8d};
    feed(connected_status, sizeof(connected_status));
    assert(radio.connection == GMK_RADIO_CONNECTED);
}
static void test_sleep_controls_never_lend_their_ack_to_battery_queries(void) {
    reset_radio();
    gmk_radio_core_enable_sleep(&radio);
    assert(gmk_radio_core_select(&radio, GMK_HOST_BT1, false) == 0);
    gmk_battery_poll();
    tx_allowance = 1;
    ack();
    assert(tx_count == 4 && radio.inflight == GMK_RADIO_TX_SLEEP_BT);
    tx_allowance = 1;
    ack();
    assert(tx_count == 5 && !radio.waiting && radio.sleep_pending == 3);
    tx_allowance = 1;
    gmk_battery_poll();
    assert(tx_count == 6 && radio.waiting && snapshot().queries == 0);
    tx_allowance      = UINT32_MAX;
    battery_test_time = 10;
    gmk_battery_poll();
    const uint8_t bt[] = {0xa6, 0x55, 0xfb};
    expect_bytes(3, bt, sizeof(bt));
    expect_bytes(6, bt, sizeof(bt));
    ack();
    const uint8_t dongle[] = {0xa6, 0x57, 0xfd};
    expect_bytes(9, dongle, sizeof(dongle));
    assert(radio.waiting && !state.waiting_ack && snapshot().queries == 0);
    ack();
    const uint8_t stopped[] = {0xa6, 0x65, 0x0b};
    expect_bytes(12, stopped, sizeof(stopped));
    assert(radio.waiting && !state.waiting_ack);
    ack();
    const uint8_t query[] = {0xa6, 0x53, 0xf9};
    expect_bytes(15, query, sizeof(query));
    assert(!radio.sleep_pending && !radio.waiting && state.waiting_ack);
}
static void test_power_quiesce_drains_uart_then_suppresses_periodic_queries(void) {
    reset_radio();
    assert(!gmk_radio_core_select(&radio, GMK_HOST_BT1, false));
    gmk_battery_poll();
    ack();
    ack();
    assert(!gmk_battery_idle() && !gmk_battery_power_quiesce(true));
    ack();
    tx_empty = false;
    assert(!gmk_battery_idle() && !gmk_battery_power_quiesce(true));
    tx_empty = true;
    assert(gmk_battery_idle() && gmk_battery_power_quiesce(true));
    unsigned reads = mmio_reads, writes = mmio_writes;
    battery_test_time = 30000;
    gmk_battery_poll();
    assert(mmio_reads == reads && mmio_writes == writes);
    assert(snapshot().queries == 1);
    assert(gmk_battery_power_quiesce(false));
    size_t before = tx_count;
    gmk_battery_poll();
    const uint8_t selection[] = {0xa6, 0x31, 0xd7};
    expect_bytes(before, selection, sizeof(selection)); /* Missing CONNECTED also requires a status refresh. */
    ack();                                              /* The reconnect SELECT owns this ACK, not the overdue query. */
    assert(snapshot().queries == 1 && !state.waiting_ack && radio.waiting);
    ack(); /* As in QMK, the queued heartbeat precedes the overdue query. */
    assert(snapshot().queries == 2 && state.waiting_ack && !radio.waiting);
    ack();
    reset_radio();
    assert(!gmk_radio_core_select(&radio, GMK_HOST_BT1, false));
    gmk_battery_poll();
    ack();
    ack();
    ack();
    const uint8_t truncated[] = {0x5c};
    feed(truncated, sizeof(truncated));
    assert(!gmk_battery_idle() && !gmk_battery_power_quiesce(true));
    reset_model(false);
    reads = mmio_reads;
    assert(gmk_battery_idle() && gmk_battery_power_quiesce(true));
    assert(mmio_reads == reads);
}
static void test_partial_mouse_frame_keeps_snapshot_and_queued_release(void) {
    connected();
    uint8_t mouse[5] = {1, 2, 0xfe, 3, 0xfd};
    assert(gmk_radio_core_mouse(&radio, mouse) == 0);
    tx_allowance = 2;
    gmk_battery_poll();
    memset(mouse, 0, sizeof(mouse));
    assert(gmk_radio_core_mouse(&radio, mouse) == 0);
    const uint8_t reply[] = {0x5c, 67, 0x9f};
    tx_allowance          = 2;
    feed(reply, sizeof(reply));
    tx_allowance = 32;
    gmk_battery_poll();
    const uint8_t pressed[] = {0xa8, 1, 2, 0xfe, 3, 0xfd, 0xa9};
    expect_bytes(0, pressed, sizeof(pressed));
    assert(tx_count == 7);
    gmk_battery_poll();
    const uint8_t acknowledgement[] = {0x61, 0x0d, 0x0a};
    expect_bytes(7, acknowledgement, sizeof(acknowledgement));
    gmk_battery_poll();
    const uint8_t release[] = {0xa8, 0, 0, 0, 0, 0, 0xa8};
    expect_bytes(10, release, sizeof(release));
    assert(tx_count == 17 && !radio.count);
}
static void test_failed_controls_release_query_scheduler_and_allow_drained_sleep(void) {
    reset_radio();
    gmk_radio_core_enable_sleep(&radio);
    assert(!gmk_radio_core_select(&radio, GMK_HOST_BT1, false));
    for (battery_test_time = 0; battery_test_time <= 2500; ++battery_test_time) {
        gmk_battery_poll(); /* No module ACKs: each acknowledged command must finish its retry budget. */
        if (radio.clear_requested) {
            radio.clear_requested = false;
            gmk_radio_core_release(&radio, false);
            gmk_radio_core_release(&radio, true);
            gmk_radio_core_release_mouse(&radio);
        }
    }
    assert(radio.faulted && snapshot().queries > 0 && !gmk_radio_core_control_busy(&radio));
    const uint8_t keyboard[8] = {0, 0, 4};
    assert(gmk_radio_core_report(&radio, false, keyboard) == -ENOTCONN);
    const uint8_t percentage[] = {0x5c, 67, 0x9f};
    feed(percentage, sizeof(percentage));
    assert(snapshot().valid && snapshot().percentage == 67);
    assert(gmk_radio_core_idle(&radio) && gmk_battery_idle());
    tx_empty = false;
    assert(!gmk_battery_idle() && !gmk_battery_power_quiesce(true));
    tx_empty                = true;
    const uint8_t partial[] = {0x5c};
    feed(partial, sizeof(partial));
    assert(!gmk_battery_idle() && !gmk_battery_power_quiesce(true));
    battery_test_time += 10;
    gmk_battery_poll();
    assert(gmk_radio_core_idle(&radio) && gmk_battery_idle() && gmk_battery_power_quiesce(true));
    size_t before = tx_count;
    battery_test_time += 10000;
    gmk_battery_poll();
    assert(tx_count == before);
}
static void test_usb_selection_maintains_charging_without_extra_wait(void) {
    reset_radio();
    assert(!gmk_radio_core_select(&radio, GMK_HOST_USB, false));
    gpio_registers[0][0x10 / 4] = 1U << 7;
    gmk_battery_poll();
    const uint8_t usb[] = {0xa6, 0x11, 0xb7}, charging[] = {0xa6, 0x64, 0x0a}, query[] = {0xa6, 0x53, 0xf9};
    expect_bytes(0, usb, sizeof(usb));
    ack();
    expect_bytes(3, charging, sizeof(charging));
    ack();
    assert(!snapshot().queries && !radio.waiting && battery_test_time == 0);
    gmk_battery_request();
    gmk_battery_poll();
    expect_bytes(6, query, sizeof(query));
    ack();
    for (unsigned heartbeat = 1; heartbeat <= 11; ++heartbeat) {
        battery_test_time = heartbeat * 1000;
        size_t before     = tx_count;
        gmk_battery_poll();
        expect_bytes(before, charging, sizeof(charging));
        ack();
    }
    assert(snapshot().queries == 1);
}
static void test_module_acknowledgements_keep_priority_in_usb(void) {
    reset_radio();
    assert(!gmk_radio_core_select(&radio, GMK_HOST_USB, false));
    gmk_battery_poll();
    ack();
    ack();
    const uint8_t percentage[] = {0x5c, 3, 0x5f}, response[] = {0x61, 0x0d, 0x0a};
    battery_test_time = 1000;
    feed(percentage, sizeof(percentage));
    expect_bytes(6, response, sizeof(response));
    assert(tx_count == 9 && !snapshot().queries);
    gmk_battery_poll();
    const uint8_t stopped[] = {0xa6, 0x65, 0x0b};
    expect_bytes(9, stopped, sizeof(stopped));
}
static void test_connected_select_drains_late_ack_before_charger_and_query(void) {
    reset_radio();
    assert(!gmk_radio_core_select(&radio, GMK_HOST_BT1, false));
    gmk_battery_poll();
    const uint8_t first[] = {0xa6, 0x31, 0xd7}, connected[] = {0x5b, 0x32, 0x8d};
    expect_bytes(0, first, sizeof(first));
    battery_test_time = 1;
    feed(connected, sizeof(connected));
    gmk_battery_poll(); /* Finish the status ACK, then retire the missing SELECT ACK. */
    assert(tx_count == 6 && !state.waiting_ack && gmk_radio_core_control_busy(&radio));
    battery_test_time = 19;
    ack();
    assert(tx_count == 6 && !state.waiting_ack);
    battery_test_time = 20;
    gmk_battery_poll();
    assert(tx_count == 6 && !snapshot().queries);
    battery_test_time = 21;
    for (unsigned frame = 0; frame < 4; ++frame)
        gmk_battery_poll();
    const uint8_t stopped[] = {0xa6, 0x65, 0x0b};
    expect_bytes(36, stopped, sizeof(stopped));
    assert(radio.waiting && !state.waiting_ack && !snapshot().queries && !radio.faulted);
    battery_test_time = 30;
    gmk_battery_poll();
    assert(radio.waiting && !state.waiting_ack && tx_count == 39);
    ack(); /* Only this new ACK can complete the charger and start the query. */
    const uint8_t query[] = {0xa6, 0x53, 0xf9};
    expect_bytes(39, query, sizeof(query));
    assert(!radio.waiting && state.waiting_ack && snapshot().queries == 1);
    ack();
    const uint8_t key[8] = {0, 0, 4};
    assert(!gmk_radio_core_report(&radio, false, key));
    size_t before = tx_count;
    gmk_battery_poll();
    const uint8_t down[] = {0xa1, 0, 0, 4, 0, 0, 0, 0, 0, 0xa5};
    expect_bytes(before, down, sizeof(down));
}
static void test_automatic_queries_follow_selected_output(void) {
    const uint8_t channels[] = {GMK_HOST_USB, GMK_HOST_BT1, GMK_HOST_BT5, GMK_HOST_DONGLE};
    for (unsigned slider = 0; slider < 2; ++slider) {
        for (unsigned channel = 0; channel < sizeof(channels); ++channel) {
            reset_radio();
            wired = slider != 0;
            assert(!gmk_radio_core_select(&radio, channels[channel], false));
            for (battery_test_time = 0; battery_test_time <= 13000; battery_test_time += 100) {
                gmk_battery_poll();
                for (unsigned pass = 0; pass < 6; ++pass) {
                    if (radio.waiting || state.waiting_ack)
                        ack();
                    else
                        gmk_battery_poll();
                }
            }
            assert(!state.waiting_ack && !radio.waiting);
            assert(snapshot().queries == (channels[channel] == GMK_HOST_USB ? 0U : 5U));
            assert(gmk_wired_mode() == wired); /* Physical write-admission gates are unchanged. */
#ifdef CONFIG_GMK70_EVENT_IDLE
            battery_test_time = channels[channel] == GMK_HOST_USB ? 13999 : state.cycle_ms + 2999;
            assert(gmk_battery_delay() == (channels[channel] == GMK_HOST_USB ? 1000U : 1U));
#endif
            if (channels[channel] == GMK_HOST_USB) {
                gmk_battery_request();
                gmk_battery_poll();
                assert(snapshot().queries == 1 && state.waiting_ack);
                ack();
            }
        }
    }
}
static void test_due_queries_are_not_starved_by_keyboard_reports(void) {
    connected();
    const uint8_t report[8]     = {4, 0, 4}; /* Alt stays physically held. */
    battery_test_time           = 3000;
    gpio_registers[0][0x10 / 4] = 1U << 7;
    assert(!gmk_radio_core_report(&radio, false, report));
    gmk_battery_poll(); /* A charger state change retains control priority. */
    assert(radio.waiting);
    ack();
    for (unsigned pass = 0; pass < 50 && !state.waiting_ack; ++pass) {
        assert(!gmk_radio_core_report(&radio, false, report));
        gmk_battery_poll();
    }
    assert(snapshot().queries == 2 && state.waiting_ack);
    assert(radio.count == 1); /* The due query wins admission before new HID work. */
    const uint8_t query[] = {0xa6, 0x53, 0xf9};
    expect_bytes(3, query, sizeof(query));
    ack();
    const uint8_t held[] = {0xa1, 4, 0, 4, 0, 0, 0, 0, 0, 0xa9};
    expect_bytes(6, held, sizeof(held));
    assert(!radio.count && !radio.waiting && !state.waiting_ack);
    const uint8_t release[8] = {0};
    assert(!gmk_radio_core_report(&radio, false, release));
    gmk_battery_poll();
    const uint8_t up[] = {0xa1, 0, 0, 0, 0, 0, 0, 0, 0, 0xa1};
    expect_bytes(16, up, sizeof(up));
}
static void test_due_query_cannot_split_nkro_or_release_barriers(void) {
    connected();
    const uint8_t keyboard[8] = {4, 0, 4}, overflow[14] = {0, 4};
    assert(!gmk_radio_core_nkro(&radio, keyboard, overflow));
    gmk_battery_poll();
    assert(tx_count == 10 && radio.overflow_pending);
    gmk_battery_request();
    gmk_battery_poll();
    const uint8_t upper[] = {0xa2, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xa6};
    expect_bytes(10, upper, sizeof(upper));
    assert(!radio.overflow_pending && snapshot().queries == 1);
    gmk_radio_core_release(&radio, false);
    gmk_radio_core_release(&radio, true);
    for (unsigned pass = 0; pass < 3; ++pass) {
        gmk_battery_poll();
        assert(snapshot().queries == 1 && !state.waiting_ack);
    }
    assert(!radio.release && tx_count == 56);
    gmk_battery_poll();
    const uint8_t query[] = {0xa6, 0x53, 0xf9};
    expect_bytes(56, query, sizeof(query));
    assert(snapshot().queries == 2);
    battery_test_time = 10;
    gmk_battery_poll();
    assert(snapshot().queries == 3); /* One bounded retry if that ACK is lost. */
    battery_test_time = 20;
    gmk_battery_poll();
    assert(!state.waiting_ack && snapshot().queries == 3 && snapshot().errors == 1);
    assert(gmk_battery_idle() && !gmk_radio_core_control_busy(&radio));
}
int main(void) {
    test_due_query_cannot_split_nkro_or_release_barriers();
    test_due_queries_are_not_starved_by_keyboard_reports();
    test_automatic_queries_follow_selected_output();
    test_connected_select_drains_late_ack_before_charger_and_query();
    test_usb_selection_maintains_charging_without_extra_wait();
    test_module_acknowledgements_keep_priority_in_usb();
    test_failed_controls_release_query_scheduler_and_allow_drained_sleep();
    test_charging_updates_the_module_without_restarting();
    test_select_then_query_ack_ownership();
    test_control_early_ack_cannot_complete_partial_transmit();
    test_partial_keyboard_ack_between_frames();
    test_nkro_overflow_is_immutable_during_partial_uart_transmit();
    test_partial_timeout_does_not_complete_frame();
    test_ten_byte_budget_and_partial_timeout_wrap();
    test_notifications_obey_checksum_raw_quarantine_and_errors();
    test_sleep_controls_never_lend_their_ack_to_battery_queries();
    test_power_quiesce_drains_uart_then_suppresses_periodic_queries();
    test_partial_mouse_frame_keeps_snapshot_and_queued_release();
    puts("Battery/radio UART: real-core serialization, ACK ownership, partial TX, parser quarantine and faults passed");
    return 0;
}
