/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void expect(struct gmk_radio_core *radio, uint32_t now, const uint8_t *bytes, uint8_t length) {
    struct gmk_radio_frame frame;
    assert(gmk_radio_core_next(radio, now, &frame, false));
    if (frame.length != length || memcmp(frame.bytes, bytes, length)) fprintf(stderr, "Radio frame at %u: expected %02x/%02x (%u bytes), got %02x/%02x (%u bytes)\n", now, bytes[0], bytes[1], length, frame.bytes[0], frame.bytes[1], frame.length);
    assert(frame.length == length && memcmp(frame.bytes, bytes, length) == 0);
    gmk_radio_core_sent(radio, now, true);
}
static void settle(struct gmk_radio_core *radio) {
    gmk_radio_core_init(radio);
    assert(gmk_radio_core_select(radio, GMK_HOST_BT1, false) == 0);
    const uint8_t select[] = {0xa6, 0x31, 0xd7};
    expect(radio, 0, select, sizeof(select));
    gmk_radio_core_ack(radio);
    gmk_radio_core_notification(radio, 0x5b, 0x32);
}
static void release_at(struct gmk_radio_core *radio, uint32_t now) {
    const uint8_t keyboard[] = {0xa1, 0, 0, 0, 0, 0, 0, 0, 0, 0xa1};
    const uint8_t nkro[]     = {0xa2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xa2};
    const uint8_t consumer[] = {0xa3, 0, 0, 0xa3};
    const uint8_t mouse[]    = {0xa8, 0, 0, 0, 0, 0, 0xa8};
    expect(radio, now, keyboard, sizeof(keyboard));
    expect(radio, now, nkro, sizeof(nkro));
    expect(radio, now, consumer, sizeof(consumer));
    expect(radio, now, mouse, sizeof(mouse));
    radio->clear_requested = false; /* Service worker consumes the native HID clear request. */
}
static void release_all(struct gmk_radio_core *radio) {
    release_at(radio, 1);
}
static void connected_host_releases_mouse_buttons(void) {
    struct gmk_radio_core radio;
    settle(&radio);
    release_all(&radio);
}
static void mouse_snapshots_share_fifo_and_release_on_loss(void) {
    struct gmk_radio_core radio;
    settle(&radio);
    release_all(&radio);
    uint8_t mouse[5] = {5, 0x81, 0x7f, 0xff, 1};
    assert(gmk_radio_core_mouse(&radio, mouse) == 0);
    memset(mouse, 0, sizeof(mouse));
    assert(gmk_radio_core_mouse(&radio, mouse) == 0);
    const uint8_t pressed[]  = {0xa8, 5, 0x81, 0x7f, 0xff, 1, 0xad};
    const uint8_t released[] = {0xa8, 0, 0, 0, 0, 0, 0xa8};
    expect(&radio, 2, pressed, sizeof(pressed));
    expect(&radio, 2, released, sizeof(released));
    assert(gmk_radio_core_idle(&radio));
    for (unsigned n = 0; n < 9; ++n)
        assert(gmk_radio_core_mouse(&radio, mouse) == (n == 8 ? -ENOBUFS : 0));
    release_all(&radio);
    expect(&radio, 2, released, sizeof(released));
    assert(gmk_radio_core_mouse(&radio, mouse) == 0);
    gmk_radio_core_notification(&radio, 0x5b, 0x33);
    assert(gmk_radio_core_mouse(&radio, mouse) == -ENOTCONN);
    release_all(&radio);
    assert(!radio.count);
    gmk_radio_core_notification(&radio, 0x5b, 0x32);
    release_all(&radio);
    assert(gmk_radio_core_mouse(&radio, mouse) == 0);
    assert(gmk_radio_core_select(&radio, GMK_HOST_BT2, false) == 0);
    release_all(&radio);
    const uint8_t selection[] = {0xa6, 0x32, 0xd8};
    expect(&radio, 3, selection, sizeof(selection));
    assert(!radio.count);
}
static void sleep_controls_hold_ack_ownership_and_retry_exact_command(void) {
    struct gmk_radio_core  radio;
    struct gmk_radio_frame frame;
    settle(&radio);
    release_all(&radio);
    gmk_radio_core_enable_sleep(&radio);
    assert(!gmk_radio_core_idle(&radio));
    const uint8_t bt[]     = {0xa6, 0x55, 0xfb};
    const uint8_t dongle[] = {0xa6, 0x57, 0xfd};
    assert(gmk_radio_core_next(&radio, 2, &frame, false));
    assert(frame.length == sizeof(bt) && !memcmp(frame.bytes, bt, sizeof(bt)));
    gmk_radio_core_ack(&radio); /* No ACK owner until the whole UART frame is sent. */
    assert(!gmk_radio_core_next(&radio, 100, &frame, false));
    gmk_radio_core_sent(&radio, 2, true);
    assert(!gmk_radio_core_next(&radio, 11, &frame, false));
    expect(&radio, 12, bt, sizeof(bt));
    gmk_radio_core_ack(&radio);
    assert(!gmk_radio_core_idle(&radio));
    expect(&radio, 13, dongle, sizeof(dongle));
    gmk_radio_core_ack(&radio);
    assert(gmk_radio_core_idle(&radio));
    assert(!gmk_radio_core_next(&radio, 14, &frame, false));
    assert(radio.active == GMK_HOST_BT1 && radio.connection == GMK_RADIO_CONNECTED);
}
static void sleep_timeout_is_bounded_and_reselection_recovers(void) {
    struct gmk_radio_core  radio;
    struct gmk_radio_frame frame;
    settle(&radio);
    release_all(&radio);
    gmk_radio_core_enable_sleep(&radio);
    const uint8_t bt[] = {0xa6, 0x55, 0xfb};
    expect(&radio, 2, bt, sizeof(bt));
    for (uint32_t n = 1; n <= 50; ++n)
        expect(&radio, 2 + n * 10, bt, sizeof(bt));
    assert(!gmk_radio_core_next(&radio, 512, &frame, false));
    assert(!radio.faulted && !gmk_radio_core_idle(&radio));
    assert(gmk_radio_core_select(&radio, GMK_HOST_BT2, false) == 0);
    release_all(&radio);
    const uint8_t selection[] = {0xa6, 0x32, 0xd8};
    expect(&radio, 513, selection, sizeof(selection));
    gmk_radio_core_ack(&radio);
    const uint8_t dongle[] = {0xa6, 0x57, 0xfd};
    expect(&radio, 514, dongle, sizeof(dongle));
    gmk_radio_core_ack(&radio);
    assert(!radio.faulted);
}
static void failed_sleep_commands_retire_without_disconnect(void) {
    struct gmk_radio_core  radio;
    struct gmk_radio_frame frame;
    settle(&radio);
    release_all(&radio);
    gmk_radio_core_enable_sleep(&radio);
    const uint8_t commands[] = {0x55, 0x57};
    for (unsigned command = 0; command < 2; ++command) {
        uint32_t now     = 2 + command * 511;
        uint8_t  bytes[] = {0xa6, commands[command], (uint8_t)(0xa6 + commands[command])};
        expect(&radio, now, bytes, sizeof(bytes));
        for (uint32_t retry = 1; retry <= 50; ++retry)
            expect(&radio, now + retry * 10, bytes, sizeof(bytes));
        assert(!gmk_radio_core_next(&radio, now + 510, &frame, false));
        assert(!radio.faulted && !radio.clear_requested && radio.connection == GMK_RADIO_CONNECTED);
    }
    assert(gmk_radio_core_idle(&radio));
    const uint8_t key[8] = {4, 0, 4};
    assert(!gmk_radio_core_report(&radio, false, key));
}
static void failed_select_requires_explicit_reselection(void) {
    struct gmk_radio_core  radio;
    struct gmk_radio_frame frame;
    gmk_radio_core_init(&radio);
    assert(!gmk_radio_core_select(&radio, GMK_HOST_BT1, false));
    assert(gmk_radio_core_next(&radio, 0, &frame, false));
    gmk_radio_core_sent(&radio, 0, false);
    assert(radio.faulted && radio.clear_requested);
    release_all(&radio);
    assert(gmk_radio_core_idle(&radio) && !gmk_radio_core_next(&radio, 60000, &frame, false));
    assert(!gmk_radio_core_select(&radio, GMK_HOST_BT1, false));
    const uint8_t selection[] = {0xa6, 0x31, 0xd7};
    expect(&radio, 60001, selection, sizeof(selection));
    gmk_radio_core_ack(&radio);
    gmk_radio_core_notification(&radio, 0x5b, 0x32);
    release_all(&radio);
    const uint8_t keyboard[8] = {0, 0, 4};
    assert(!gmk_radio_core_report(&radio, false, keyboard));
    const uint8_t pressed[] = {0xa1, 0, 0, 4, 0, 0, 0, 0, 0, 0xa5};
    expect(&radio, 60002, pressed, sizeof(pressed));
}
static void failed_pair_is_not_replayed(void) {
    struct gmk_radio_core  radio;
    struct gmk_radio_frame frame;
    settle(&radio);
    release_all(&radio);
    assert(!gmk_radio_core_select(&radio, GMK_HOST_BT1, true));
    release_all(&radio);
    const uint8_t pair[] = {0xa6, 0x51, 0xf7};
    expect(&radio, 2, pair, sizeof(pair));
    for (uint32_t retry = 1; retry <= 50; ++retry)
        expect(&radio, 2 + retry * 10, pair, sizeof(pair));
    assert(!gmk_radio_core_next(&radio, 512, &frame, false));
    release_all(&radio);
    assert(radio.faulted && gmk_radio_core_idle(&radio));
    assert(!gmk_radio_core_next(&radio, 60000, &frame, false));
}
static void failed_zero_releases_do_not_recreate_clear_work(void) {
    struct gmk_radio_core  radio;
    struct gmk_radio_frame frame;
    settle(&radio);
    release_all(&radio);
    const uint8_t keyboard[8] = {0, 0, 4};
    assert(!gmk_radio_core_report(&radio, false, keyboard));
    assert(gmk_radio_core_next(&radio, 2, &frame, false));
    gmk_radio_core_sent(&radio, 2, false);
    assert(radio.clear_requested && !gmk_radio_core_idle(&radio));
    radio.clear_requested = false;
    for (uint32_t release = 0; release < 4; ++release) {
        assert(gmk_radio_core_next(&radio, 3 + release, &frame, false));
        assert(frame.bytes[0] == 0xa1 || frame.bytes[0] == 0xa2 || frame.bytes[0] == 0xa3 || frame.bytes[0] == 0xa8);
        for (uint8_t byte = 1; byte < frame.length - 1; ++byte)
            assert(!frame.bytes[byte]);
        gmk_radio_core_sent(&radio, 3 + release, false);
        assert(!radio.clear_requested);
    }
    assert(radio.faulted && gmk_radio_core_idle(&radio));
    assert(!gmk_radio_core_next(&radio, 60000, &frame, false));
}
static void failed_charger_transmit_waits_for_next_heartbeat(void) {
    struct gmk_radio_core  radio;
    struct gmk_radio_frame frame;
    settle(&radio);
    release_all(&radio);
    gmk_radio_core_charger(&radio, false, false);
    assert(gmk_radio_core_next(&radio, UINT32_MAX - 2, &frame, false));
    gmk_radio_core_sent(&radio, UINT32_MAX - 2, false);
    release_all(&radio);
    assert(!gmk_radio_core_control_busy(&radio) && gmk_radio_core_idle(&radio));
    assert(!gmk_radio_core_next(&radio, 996, &frame, false));
    const uint8_t stopped[] = {0xa6, 0x65, 0x0b};
    expect(&radio, 997, stopped, sizeof(stopped));
    assert(!gmk_radio_core_idle(&radio));
    gmk_radio_core_ack(&radio);
    assert(gmk_radio_core_idle(&radio));
}
static void usb_selection_has_no_extra_quiet_interval_across_wrap(void) {
    struct gmk_radio_core  radio;
    struct gmk_radio_frame frame;
    gmk_radio_core_init(&radio);
    assert(!gmk_radio_core_select(&radio, GMK_HOST_USB, false));
    const uint8_t usb[] = {0xa6, 0x11, 0xb7};
    expect(&radio, UINT32_MAX - 5, usb, sizeof(usb));
    expect(&radio, 4, usb, sizeof(usb));
    gmk_radio_core_ack(&radio);
    assert(!gmk_radio_core_control_busy(&radio));
    gmk_radio_core_charger(&radio, true, false);
    const uint8_t charging[] = {0xa6, 0x64, 0x0a};
    expect(&radio, 4, charging, sizeof(charging));
    gmk_radio_core_ack(&radio);
    assert(!gmk_radio_core_next(&radio, 5, &frame, false));
}
static void wireless_selection_follows_usb_without_extra_delay(void) {
    struct gmk_radio_core  radio;
    struct gmk_radio_frame frame;
    gmk_radio_core_init(&radio);
    assert(gmk_radio_core_select(&radio, GMK_HOST_USB, false) == 0);
    const uint8_t usb[] = {0xa6, 0x11, 0xb7};
    expect(&radio, 0, usb, sizeof(usb));
    gmk_radio_core_ack(&radio);
    assert(!gmk_radio_core_control_busy(&radio));
    assert(!gmk_radio_core_select(&radio, GMK_HOST_BT1, false));
    const uint8_t bluetooth[] = {0xa6, 0x31, 0xd7};
    expect(&radio, 1, bluetooth, sizeof(bluetooth));
    gmk_radio_core_ack(&radio);
    assert(!gmk_radio_core_control_busy(&radio));
    assert(!gmk_radio_core_next(&radio, 2, &frame, false));
    gmk_radio_core_notification(&radio, 0x5b, 0x32);
    release_all(&radio);
    const uint8_t keyboard[8] = {0, 0, 4};
    assert(!gmk_radio_core_report(&radio, false, keyboard));
    const uint8_t pressed[] = {0xa1, 0, 0, 4, 0, 0, 0, 0, 0, 0xa5};
    expect(&radio, 2, pressed, sizeof(pressed));
}
static void failed_usb_selection_does_not_leave_a_quiet_lease(void) {
    struct gmk_radio_core  radio;
    struct gmk_radio_frame frame;
    gmk_radio_core_init(&radio);
    assert(gmk_radio_core_select(&radio, GMK_HOST_USB, false) == 0);
    const uint8_t usb[] = {0xa6, 0x11, 0xb7};
    expect(&radio, 0, usb, sizeof(usb));
    for (uint32_t retry = 1; retry <= 50; ++retry)
        expect(&radio, retry * 10, usb, sizeof(usb));
    assert(!gmk_radio_core_next(&radio, 510, &frame, false));
    assert(radio.faulted && radio.clear_requested);
    radio.clear_requested = false;
    assert(!gmk_radio_core_control_busy(&radio) && gmk_radio_core_idle(&radio));
    assert(!gmk_radio_core_next(&radio, 511, &frame, false));
}
static void superseding_usb_selection_drains_old_ack_without_extra_delay(void) {
    struct gmk_radio_core  radio;
    struct gmk_radio_frame frame;
    settle(&radio);
    release_all(&radio);
    assert(!gmk_radio_core_select(&radio, GMK_HOST_BT2, false));
    release_all(&radio);
    const uint8_t bluetooth[] = {0xa6, 0x32, 0xd8}, usb[] = {0xa6, 0x11, 0xb7};
    expect(&radio, 2, bluetooth, sizeof(bluetooth));
    assert(!gmk_radio_core_select(&radio, GMK_HOST_USB, false));
    assert(!gmk_radio_core_next(&radio, 3, &frame, false));
    gmk_radio_core_ack(&radio);
    assert(!gmk_radio_core_next(&radio, 22, &frame, false));
    release_at(&radio, 23);
    expect(&radio, 23, usb, sizeof(usb));
    assert(radio.waiting && !radio.faulted);
    gmk_radio_core_ack(&radio);
    assert(!gmk_radio_core_next(&radio, 24, &frame, false) && !gmk_radio_core_control_busy(&radio));
    assert(!gmk_radio_core_next(&radio, 5523, &frame, false) && !gmk_radio_core_control_busy(&radio));
}
static void pairing_remains_busy_after_its_control_ack(void) {
    struct gmk_radio_core radio;
    settle(&radio);
    release_all(&radio);
    assert(gmk_radio_core_select(&radio, GMK_HOST_BT1, true) == 0);
    release_all(&radio);
    const uint8_t pair[] = {0xa6, 0x51, 0xf7};
    expect(&radio, 2, pair, sizeof(pair));
    gmk_radio_core_ack(&radio);
    assert(!gmk_radio_core_idle(&radio));
}
static void charging_uses_qmk_fifty_retries_without_disconnect(void) {
    struct gmk_radio_core  radio;
    struct gmk_radio_frame frame;
    settle(&radio);
    release_all(&radio);
    gmk_radio_core_charger(&radio, true, false);
    const uint8_t charging[] = {0xa6, 0x64, 0x0a};
    expect(&radio, 2, charging, sizeof(charging));
    const uint8_t key[8] = {4, 0, 4};
    assert(!gmk_radio_core_report(&radio, false, key));
    for (uint32_t retry = 1; retry <= 50; ++retry)
        expect(&radio, 2 + retry * 10, charging, sizeof(charging));
    assert(!gmk_radio_core_next(&radio, 512, &frame, false));
    assert(!radio.faulted && radio.connection == GMK_RADIO_CONNECTED);
    const uint8_t pressed[] = {0xa1, 4, 0, 4, 0, 0, 0, 0, 0, 0xa9};
    expect(&radio, 513, pressed, sizeof(pressed));
    expect(&radio, 1502, charging, sizeof(charging));
}
static void charging_frames_keep_their_state_across_changes_and_wrap(void) {
    struct gmk_radio_core radio;
    settle(&radio);
    release_all(&radio);
    gmk_radio_core_charger(&radio, true, false);
    struct gmk_radio_frame frame;
    assert(gmk_radio_core_next(&radio, UINT32_MAX - 2, &frame, false));
    gmk_radio_core_charger(&radio, true, true);
    gmk_radio_core_ack(&radio); /* No ACK ownership before this frame completes. */
    const uint8_t charging[] = {0xa6, 0x64, 0x0a};
    assert(!memcmp(frame.bytes, charging, sizeof(charging)) && !radio.waiting);
    gmk_radio_core_sent(&radio, UINT32_MAX - 2, true);
    gmk_radio_core_ack(&radio);
    const uint8_t full[] = {0xa6, 0x66, 0x0c};
    expect(&radio, UINT32_MAX - 1, full, sizeof(full));
    gmk_radio_core_ack(&radio);
    assert(!gmk_radio_core_next(&radio, 997, &frame, false));
    expect(&radio, 998, full, sizeof(full));
    gmk_radio_core_ack(&radio);
    gmk_radio_core_charger(&radio, false, true);
    const uint8_t stopped[] = {0xa6, 0x65, 0x0b};
    expect(&radio, 998, stopped, sizeof(stopped));
    gmk_radio_core_ack(&radio);
    assert(gmk_radio_core_idle(&radio));
}
static void stop_selection_matches_qmk_predicates_and_preserves_destination(void) {
    for (unsigned flags = 0; flags < 16; ++flags) {
        struct gmk_radio_core  radio;
        struct gmk_radio_frame frame;
        gmk_radio_core_init(&radio);
        radio.selected = radio.active = (flags & 1) ? GMK_HOST_USB : GMK_HOST_BT2;
        radio.connection              = (flags & 2) ? GMK_RADIO_CONNECTED : GMK_RADIO_DISCONNECTED;
        bool special = (flags & 4) != 0, cable = (flags & 8) != 0;
        bool expected = !(radio.selected == GMK_HOST_USB && cable) && (special || radio.connection != GMK_RADIO_CONNECTED);
        assert(gmk_radio_core_prepare_stop(&radio, special, cable) == expected);
        if (!expected) continue;
        const uint8_t usb[]    = {0xa6, 0x11, 0xb7};
        uint8_t       selected = radio.selected;
        /* A reconnect timer expiring during pre-STOP must not enqueue a
         * selection behind STOP and prevent the UART owner from draining. */
        radio.connect_attempts = 3;
        radio.connect_at       = 0;
        expect(&radio, 5000, usb, sizeof(usb));
        assert(radio.waiting && radio.selected == selected);
        gmk_radio_core_ack(&radio);
        assert(!gmk_radio_core_control_busy(&radio));
        assert(gmk_radio_core_next(&radio, 100000, &frame, true) == GMK_RADIO_TURN_NONE);
        gmk_radio_core_wake(&radio);
        assert(!radio.stopping && radio.selected == selected);
    }
}
int main(void) {
    stop_selection_matches_qmk_predicates_and_preserves_destination();
    superseding_usb_selection_drains_old_ack_without_extra_delay();
    usb_selection_has_no_extra_quiet_interval_across_wrap();
    wireless_selection_follows_usb_without_extra_delay();
    failed_usb_selection_does_not_leave_a_quiet_lease();
    failed_sleep_commands_retire_without_disconnect();
    failed_select_requires_explicit_reselection();
    failed_pair_is_not_replayed();
    failed_zero_releases_do_not_recreate_clear_work();
    failed_charger_transmit_waits_for_next_heartbeat();
    charging_uses_qmk_fifty_retries_without_disconnect();
    charging_frames_keep_their_state_across_changes_and_wrap();
    connected_host_releases_mouse_buttons();
    mouse_snapshots_share_fifo_and_release_on_loss();
    sleep_controls_hold_ack_ownership_and_retry_exact_command();
    sleep_timeout_is_bounded_and_reselection_recovers();
    pairing_remains_busy_after_its_control_ack();
    puts("Radio parity: stock mouse FIFO/releases, sleep ACK ownership and bounded recovery passed");
    return 0;
}
