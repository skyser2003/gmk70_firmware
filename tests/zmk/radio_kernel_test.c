/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zmk/events/keycode_state_changed.h>
#include "../../firmware/zmk/radio/radio.c"

uint32_t radio_test_time;
unsigned radio_test_locked;
#ifdef CONFIG_GMK70_EVENT_IDLE
void gmk_service_notify(void) {}
void gmk_scan_notify(void) {}
#endif
static uint8_t                        physical_raw = 3;
static bool                           left         = true, in_work;
static unsigned                       clears, events;
static bool                           inject;
static bool                           inject_release;
static unsigned                       fail_marker_edge;
static struct zmk_hid_keyboard_report keyboard;
static struct zmk_hid_consumer_report consumer;
static uint8_t                        explicit_mods;
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
static bool ids_confirmed = true;
bool        gmk_zmk_settings_ids_ready(void) {
    return ids_confirmed;
}
#endif
#ifdef CONFIG_GMK70_CONTROLS
static uint8_t  controls_selected, controls_connection, controls_leds;
static unsigned controls_led_updates;
void            gmk_controls_radio_state(uint8_t selected, uint8_t connection) {
    assert(!radio_test_locked);
    controls_selected   = selected;
    controls_connection = connection;
}
void gmk_controls_radio_leds(uint8_t leds) {
    assert(!radio_test_locked && controls_selected == core.selected && controls_connection == GMK_RADIO_CONNECTED);
    controls_leds = leds;
    ++controls_led_updates;
}
#endif
bool gmk_wired_is_left(void) {
    return left;
}
uint8_t gmk_host_switch(void) {
    return physical_raw;
}
struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void) {
    return &keyboard;
}
struct zmk_hid_consumer_report *zmk_hid_get_consumer_report(void) {
    return &consumer;
}
uint8_t zmk_hid_get_explicit_mods(void) {
    return explicit_mods;
}
void gmk_radio_clear_hid(void) {
    assert(in_work && !radio_test_locked);
    ++clears;
    keyboard.body = (struct zmk_hid_keyboard_report_body){0};
    consumer.body = (struct zmk_hid_consumer_report_body){0};
    gmk_radio_release(false);
    gmk_radio_release(true);
    if (inject_release) {
        inject_release = false;
        gmk_radio_unhold(9);
    }
    if (inject) {
        inject = false;
        assert(gmk_radio_select(5, false) == 0);
    }
}
#ifndef CONFIG_GMK70_NATIVE_KEYMAP_TEST
int __real_zmk_keymap_set_layer_binding_at_idx(zmk_keymap_layer_id_t layer, uint16_t index, const struct zmk_behavior_binding binding) {
    (void)layer;
    (void)index;
    (void)binding;
    assert(!radio_test_locked);
    return 17;
}
#endif
#ifdef CONFIG_GMK70_NATIVE_KEYMAP_TEST
int              keymap_init(void);
static const int binding_result = 0;
#else
static const int binding_result = 17;
#endif
int raise_zmk_keycode_state_changed(struct zmk_keycode_state_changed event) {
    assert(in_work && !radio_test_locked && event.usage_page == 7 && event.keycode == 0x73);
    assert(!event.implicit_modifiers && !event.explicit_modifiers);
    assert(event.state == (events % 2 == 0));
    ++events;
    if (events == fail_marker_edge) return -EIO;
    uint8_t report[8] = {0};
    if (event.state) report[2] = 0x73;
    return gmk_radio_submit_keyboard(report);
}
static void run_work(void) {
    for (unsigned pass = 0; work.pending && pass < 8; ++pass) {
        work.pending = false;
        in_work      = true;
        work.handler(&work);
        in_work = false;
    }
    assert(!work.pending);
}
static void start(bool is_left) {
    left            = is_left;
    physical_raw    = 3;
    radio_test_time = 0;
    clears = events = explicit_mods = 0;
    fail_marker_edge                = 0;
    inject = inject_release = false;
    keyboard                = (struct zmk_hid_keyboard_report){0};
    consumer                = (struct zmk_hid_consumer_report){0};
    assert(registered_init() == 0);
}
static void settle(void) {
    struct gmk_radio_frame frame;
    for (unsigned n = 0; n < 20 && gmk_radio_next(radio_test_time, &frame); ++n) {
        gmk_radio_sent(radio_test_time, true);
        gmk_radio_ack();
    }
}
static void connect_radio(void) {
    assert(gmk_radio_select(2, false) == 0);
    run_work();
    settle();
    gmk_radio_notification(0x5b, 0x32);
    gmk_radio_poll();
    run_work();
    settle();
}
static void async_selection_and_latest_request(void) {
    start(true);
    assert(gmk_radio_select(7, false) == -EINVAL);
    assert(gmk_radio_select(0, true) == -EINVAL);
    assert(gmk_radio_select(2, false) == 0);
    struct gmk_radio_status status;
    gmk_radio_get(&status);
    assert(status.waiting && !gmk_radio_selected() && clears == 0);
    assert(gmk_radio_select(4, false) == 0);
    inject = true;
    run_work();
    gmk_radio_get(&status);
    assert(status.selected == 5 && clears >= 1);
    start(false);
    assert(gmk_radio_select(1, false) == -ENODEV);
    const uint8_t zero[8] = {0};
    assert(gmk_radio_submit_keyboard(zero) == -ENODEV);
}
static void mode_and_hold_work_remain_deferred(void) {
    start(true);
    gmk_radio_poll();
    radio_test_time = 49;
    gmk_radio_poll();
    assert(clears == 0 && !work.pending);
    radio_test_time = 50;
    gmk_radio_poll();
    assert(clears == 0 && work.pending);
    run_work();
    assert(gmk_radio_hold(3, 1) == 0);
    run_work();
    settle();
    radio_test_time = 3049;
    gmk_radio_poll();
    run_work();
    radio_test_time = 3050;
    gmk_radio_poll();
    physical_raw = 1;
    run_work();
    struct gmk_radio_status status;
    gmk_radio_get(&status);
    assert(!core.pending_pair);
    assert(gmk_radio_hold(3, 1) == 0);
    gmk_radio_unhold(1);
    run_work();
    radio_test_time += 3000;
    gmk_radio_poll();
    run_work();
    assert(!core.pending_pair);
    assert(gmk_radio_hold(3, 1) == 0);
    run_work();
    const struct zmk_behavior_binding binding = {.behavior_dev = "replacement"};
    assert(__wrap_zmk_keymap_set_layer_binding_at_idx(0, 1, binding) == binding_result);
    radio_test_time += 3000;
    gmk_radio_poll();
    run_work();
    assert(!core.pending_pair);
}
static void notifications_and_fifo_use_existing_core(void) {
    start(true);
    connect_radio();
    const uint8_t pressed[8] = {0, 0, 4}, released[8] = {0};
    assert(gmk_radio_submit_keyboard(pressed) == 0);
    assert(gmk_radio_submit_keyboard(released) == 0);
    assert(!gmk_radio_control_busy());
    struct gmk_radio_frame frame;
    assert(gmk_radio_next(0, &frame) && frame.bytes[0] == 0xa1 && frame.bytes[3] == 4);
    gmk_radio_sent(0, true);
    assert(gmk_radio_next(0, &frame) && frame.bytes[3] == 0);
    gmk_radio_sent(0, true);
    const unsigned previous = clears;
    gmk_radio_notification(0x5b, 0x33);
    gmk_radio_poll();
    assert(clears == previous && work.pending);
    run_work();
    assert(clears == previous + 1);
    physical_raw = 0;
    assert(gmk_radio_submit_keyboard(pressed) < 0);
}
static void nkro_submission_preserves_both_reports_and_role_gates(void) {
    start(false);
    uint8_t keyboard[8]  = {2, 0, 4};
    uint8_t overflow[14] = {0, 4};
    assert(gmk_radio_submit_nkro(keyboard, overflow) == -ENODEV);
    start(true);
    connect_radio();
    assert(gmk_radio_submit_nkro(keyboard, overflow) == 0);
    memset(keyboard, 0, sizeof(keyboard));
    memset(overflow, 0, sizeof(overflow));
    struct gmk_radio_frame frame;
    assert(gmk_radio_next(0, &frame) && frame.length == 10 && frame.bytes[0] == 0xa1 && frame.bytes[1] == 2 && frame.bytes[3] == 4);
    gmk_radio_sent(0, true);
    assert(gmk_radio_next(0, &frame) && frame.length == 16 && frame.bytes[0] == 0xa2 && frame.bytes[2] == 4 && frame.bytes[15] == 0xa6);
    gmk_radio_sent(0, true);
    physical_raw = 0;
    assert(gmk_radio_submit_nkro(keyboard, overflow) == -EPERM);
}
static void release_after_timeout_cancels_pair_before_syswork(void) {
    start(true);
    gmk_radio_poll();
    radio_test_time = 50;
    gmk_radio_poll();
    run_work();
    settle();
    assert(gmk_radio_hold(3, 9) == 0);
    run_work();
    settle();
    radio_test_time = 3050;
    gmk_radio_poll();
    assert(work.pending);
    gmk_radio_unhold(9);
    run_work();
    assert(!core.pending_pair);
    assert(gmk_radio_hold(3, 9) == 0);
    run_work();
    settle();
    radio_test_time = 6050;
    gmk_radio_poll();
    inject_release = true;
    run_work();
    assert(!core.pending_pair);
}
static void hold_pairs_once_at_three_seconds_in_system_context(void) {
    start(true);
    gmk_radio_poll();
    radio_test_time = 50;
    gmk_radio_poll();
    run_work();
    settle();
    assert(gmk_radio_hold(3, 4) == 0);
    run_work();
    settle();
    radio_test_time = 3049;
    gmk_radio_poll();
    run_work();
    assert(!core.pending_pair);
    radio_test_time = 3050;
    gmk_radio_poll();
    assert(!core.pending_pair);
    run_work();
    assert(core.pending_pair && gmk_radio_control_busy());
    settle();
    assert(!core.pending_pair);
    radio_test_time = 9000;
    gmk_radio_poll();
    run_work();
    assert(!core.pending_pair);
}
static void canceled_pair_after_syswork_preserves_connected_typing(void) {
    for (unsigned reassign = 0; reassign < 2; ++reassign) {
        start(true);
        gmk_radio_poll();
        radio_test_time = 50;
        gmk_radio_poll();
        run_work();
        settle();
        connect_radio();
        assert(gmk_radio_hold(2, 9) == 0);
        run_work();
        settle();
        radio_test_time = 3050;
        gmk_radio_poll();
        run_work();
        assert(core.pending_pair);
        if (reassign) {
            const struct zmk_behavior_binding replacement = {0};
            assert(__wrap_zmk_keymap_set_layer_binding_at_idx(1, 9, replacement) == binding_result);
        } else
            gmk_radio_unhold(9);
        settle();
        const uint8_t pressed[8] = {0, 0, 4};
        assert(!core.pending_pair && core.connection == GMK_RADIO_CONNECTED);
        assert(gmk_radio_submit_keyboard(pressed) == 0);
        assert(gmk_radio_select(2, false) == 0);
        run_work();
        settle();
        assert(gmk_radio_submit_keyboard(pressed) == 0);
    }
}
static void status_waits_while_control_retry_is_being_transmitted(void) {
    start(true);
    assert(gmk_radio_select(2, false) == 0);
    run_work();
    struct gmk_radio_frame frame;
    assert(gmk_radio_next(0, &frame) && frame.bytes[0] == 0xa6);
    gmk_radio_sent(0, true);
    assert(gmk_radio_next(10, &frame) && frame.bytes[0] == 0xa6);
    struct gmk_radio_status status;
    gmk_radio_get(&status);
    assert(status.waiting && gmk_radio_control_busy());
}
static void power_quiesce_requires_drained_reports_and_blocks_new_work(void) {
    start(true);
    assert(!gmk_radio_idle() && !gmk_radio_power_quiesce(true));
    connect_radio();
    const uint8_t keyboard_report[8] = {0, 0, 4};
    assert(gmk_radio_submit_keyboard(keyboard_report) == 0);
    assert(!gmk_radio_idle() && !gmk_radio_power_quiesce(true));
    settle();
    assert(gmk_radio_idle() && gmk_radio_power_quiesce(true));
    assert(gmk_radio_submit_keyboard(keyboard_report) == -EBUSY);
    assert(gmk_radio_select(3, false) == -EBUSY);
    assert(gmk_radio_hold(3, 9) == -EBUSY);
    assert(gmk_radio_power_quiesce(false));
    assert(gmk_radio_submit_keyboard(keyboard_report) == 0);
    start(false);
    assert(gmk_radio_idle() && gmk_radio_power_quiesce(true));
}
static void wake_keeps_native_modifiers_and_is_silent_while_paused(void) {
    start(true);
    connect_radio();
    const unsigned before = clears;
    assert(gmk_radio_idle() && gmk_radio_power_quiesce(true));
    gmk_radio_wake();
    struct gmk_radio_frame frame;
    assert(!gmk_radio_next(radio_test_time + 100000, &frame));
    assert(gmk_radio_submit_keyboard((const uint8_t[8]){0}) == -EBUSY);
    assert(gmk_radio_power_quiesce(false));
    keyboard.body.modifiers = 4;
    assert(!gmk_radio_submit_keyboard((const uint8_t[8]){4, 0, 0x2b}));
    assert(!gmk_radio_submit_keyboard((const uint8_t[8]){4}));
    settle(); /* SELECT current host, with all transport ACKs but no CONNECTED. */
    assert(core.resuming && core.count == 2 && core.selected == 2 && core.active == 2);
    gmk_radio_notification(0x5b, 0x32);
    gmk_radio_poll();
    run_work();
    assert(clears == before && keyboard.body.modifiers == 4 && core.count == 2);
    settle();
    assert(!core.resuming && !core.count && !core.pending_pair);
    gmk_radio_wake(); /* A stray callback outside a paused wake has no effect. */
    assert(!core.pending_select);
}
static void restoring_bt_slot_does_not_select_or_transmit(void) {
    start(true);
    assert(gmk_radio_restore_last_bt(0) == -EINVAL);
    assert(gmk_radio_restore_last_bt(6) == -EINVAL);
    assert(gmk_radio_restore_last_bt(5) == 0);
    assert(gmk_radio_last_bt() == 5 && core.selected == GMK_HOST_USB);
    connect_radio();
    assert(gmk_radio_last_bt() == 2);
}
static void slider_stable(uint8_t raw) {
    physical_raw = raw;
    gmk_radio_poll();
    run_work();
    radio_test_time += 50;
    gmk_radio_poll();
    run_work();
}
static void slider_send_select(uint8_t command) {
    struct gmk_radio_frame frame;
    for (unsigned count = 0; count < 6; ++count) {
        assert(gmk_radio_next(radio_test_time, &frame));
        gmk_radio_sent(radio_test_time, true);
        if (frame.bytes[0] == 0xa6) {
            assert(frame.bytes[1] == command);
            return;
        }
        assert(frame.bytes[0] == 0xa1 || frame.bytes[0] == 0xa2 || frame.bytes[0] == 0xa3 || frame.bytes[0] == 0xa8);
    }
    assert(!"slider did not dispatch its selected channel");
}
static void slider_transition_retires_an_old_select_lease(void) {
    static const struct slider_case {
        uint8_t start_raw, next_raw, first_command, next_command, selected;
    } cases[] = {{1, 2, 0x30, 0x31, GMK_HOST_BT1}, {2, 1, 0x31, 0x30, GMK_HOST_DONGLE}, {3, 2, 0x11, 0x31, GMK_HOST_BT1}, {2, 3, 0x31, 0x11, GMK_HOST_USB}};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        start(true);
        slider_stable(cases[i].start_raw);
        slider_send_select(cases[i].first_command);
        physical_raw = 0; /* Invalid contact between slider detents cannot select or pair. */
        ++radio_test_time;
        gmk_radio_poll();
        run_work();
        slider_stable(cases[i].next_raw);
        struct gmk_radio_frame frame;
        assert(core.selected == cases[i].selected && !core.pending_pair);
        assert(!gmk_radio_next(radio_test_time, &frame));
        gmk_radio_ack();
        radio_test_time += 19;
        assert(!gmk_radio_next(radio_test_time, &frame));
        ++radio_test_time;
        slider_send_select(cases[i].next_command);
        assert(core.selected == cases[i].selected && core.active == cases[i].selected && core.waiting && !core.faulted);
    }
}
static void slider_bypasses_usb_quiet_without_reusing_its_ack(void) {
    start(true);
    slider_stable(3);
    slider_send_select(0x11);
    gmk_radio_ack();
    struct gmk_radio_frame frame;
    ++radio_test_time;
    assert(!gmk_radio_next(radio_test_time, &frame) && gmk_radio_control_busy());
    slider_stable(2);
    assert(radio_test_time < 5500);
    slider_send_select(0x31);
    gmk_radio_ack();
    gmk_radio_notification(0x5b, 0x32);
    gmk_radio_poll();
    run_work();
    settle();
    const uint8_t key[8] = {0, 0, 4};
    assert(!gmk_radio_submit_keyboard(key));
}
#ifdef CONFIG_GMK70_CONTROLS
static void host_leds_require_an_attributed_connected_host(void) {
    start(true);
    controls_led_updates = 0;
    gmk_radio_notification(0x5a, 2);
    assert(controls_led_updates == 0);
    connect_radio();
    gmk_radio_notification(0x5a, 2);
    assert(controls_led_updates == 1 && controls_leds == 2 && controls_selected == 2);
    gmk_radio_notification(0x5b, 0x33);
    gmk_radio_notification(0x5a, 0);
    assert(controls_led_updates == 1);
    gmk_radio_notification(0x5b, 0x32);
    gmk_radio_notification(0x5a, 0);
    assert(controls_led_updates == 2 && controls_leds == 0);
    assert(gmk_radio_select(3, false) == 0);
    run_work();
    gmk_radio_notification(0x5a, 2);
    assert(controls_led_updates == 2);
    settle();
    gmk_radio_notification(0x5b, 0x32);
    gmk_radio_notification(0x5a, 1);
    assert(controls_led_updates == 3 && controls_selected == 3);
    start(false);
    gmk_radio_notification(0x5a, 2);
    assert(controls_led_updates == 3);
}
#endif
#ifdef CONFIG_GMK70_RADIO_QUALIFICATION
static void qualification_marker_is_only_fixed_f24_and_respects_held_input(void) {
    start(true);
    connect_radio();
    keyboard.body.keys[0] = 4;
    assert(gmk_radio_test_marker() == -EBUSY);
    keyboard.body.keys[0] = 0;
    explicit_mods         = 1;
    assert(gmk_radio_test_marker() == -EBUSY);
    explicit_mods         = 0;
    consumer.body.keys[0] = 0x123;
    assert(gmk_radio_test_marker() == -EBUSY);
    consumer.body.keys[0] = 0;
    assert(gmk_radio_test_marker() == 0 && events == 0);
    assert(gmk_radio_test_marker() == -EBUSY);
    run_work();
    assert(events == 2);
    struct gmk_radio_frame frame;
    assert(gmk_radio_next(0, &frame) && frame.bytes[3] == 0x73);
    gmk_radio_sent(0, true);
    assert(gmk_radio_next(0, &frame) && frame.bytes[3] == 0);
    gmk_radio_sent(0, true);
    assert(gmk_radio_test_marker() == 0);
    keyboard.body.keys[0] = 4;
    run_work();
    assert(events == 2);
}
static void qualification_marker_waits_for_control_and_releases_after_failure(void) {
    start(true);
    connect_radio();
    gmk_radio_charger_state(true, false);
    assert(gmk_radio_test_marker() == -ENOTCONN && events == 0);
    settle();
    for (unsigned edge = 1; edge <= 2; ++edge) {
        start(true);
        connect_radio();
        unsigned before  = clears;
        fail_marker_edge = edge;
        assert(gmk_radio_test_marker() == 0);
        run_work();
        assert(events == 2 && clears == before + 1);
    }
}
#endif
#ifdef CONFIG_GMK70_NATIVE_KEYMAP_TEST
#    include "radio_native_keymap_fixture.h"
static void native_shortcut_taps_switch_profiles_during_an_old_select(void) {
    for (unsigned reverse = 0; reverse < 2; ++reverse) {
        settings_block = map_block = false;
        reload_result              = 0;
        map_result                 = 16;
        start(true);
        assert(!keymap_init());
        uint8_t first = reverse ? GMK_HOST_BT2 : GMK_HOST_BT1;
        uint8_t next  = reverse ? GMK_HOST_BT1 : GMK_HOST_BT2;
        assert(!gmk_radio_restore_last_bt(first));
        slider_stable(2);
        const struct zmk_behavior_binding first_binding = {.behavior_dev = "radio", .param1 = first};
        const struct zmk_behavior_binding next_binding  = {.behavior_dev = "radio", .param1 = next};
        assert(!zmk_keymap_set_layer_binding_at_idx(0, 9, first_binding));
        assert(!zmk_keymap_set_layer_binding_at_idx(0, 10, next_binding));
        struct zmk_behavior_binding       first_key = *zmk_keymap_get_layer_binding_at_idx(0, 9);
        struct zmk_behavior_binding       next_key  = *zmk_keymap_get_layer_binding_at_idx(0, 10);
        struct zmk_behavior_binding_event event     = {.position = 9, .timestamp = radio_test_time};
        assert(!registered_api->binding_pressed(&first_key, event));
        run_work();
        slider_send_select(reverse ? 0x32 : 0x31);
        radio_test_time += 10;
        event.timestamp = radio_test_time;
        assert(!registered_api->binding_released(&first_key, event));
        event.position = 10;
        assert(!registered_api->binding_pressed(&next_key, event));
        radio_test_time += 10;
        event.timestamp = radio_test_time;
        assert(!registered_api->binding_released(&next_key, event));
        run_work();
        assert(core.selected == next && !core.pending_pair && !selector.holding);
        struct gmk_radio_frame frame;
        assert(!gmk_radio_next(radio_test_time, &frame));
        gmk_radio_ack(); /* Old profile's delayed ACK must not complete the next one. */
        radio_test_time += 19;
        assert(!gmk_radio_next(radio_test_time, &frame));
        ++radio_test_time;
        slider_send_select(reverse ? 0x31 : 0x32);
        assert(core.waiting && core.active == next && !core.pending_pair);
        gmk_radio_ack();
        gmk_radio_notification(0x5b, 0x32);
        gmk_radio_poll();
        run_work();
        settle();
        radio_test_time += 3000;
        gmk_radio_poll();
        run_work();
        settle();
        assert(!selector.holding && !core.pending_pair && !core.faulted);
        const uint8_t key[8] = {0, 0, 4};
        assert(!gmk_radio_submit_keyboard(key));
    }
}
#endif
static void unsaved_native_ids_block_binding_mutation(void) {
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
    start(true);
    ids_confirmed                           = false;
    struct zmk_behavior_binding replacement = {.behavior_dev = "replacement", .param1 = 4};
    assert(__wrap_zmk_keymap_set_layer_binding_at_idx(0, 1, replacement) == -EACCES);
    ids_confirmed = true;
    assert(__wrap_zmk_keymap_set_layer_binding_at_idx(0, 1, replacement) == binding_result);
#endif
}
static void disconnected_sleep_deadline(void) {
    start(true);
    radio_test_time = 100000;
    assert(!gmk_radio_disconnected_timeout()); /* Unknown/USB is not disconnect. */
    assert(!gmk_radio_select(2, false));
    run_work();
    settle();
    assert(!gmk_radio_disconnected_timeout()); /* A SELECT ACK is not status. */
    radio_test_time = UINT32_MAX - 15000U;
    gmk_radio_notification(0x5b, 0x33);
    assert(!gmk_radio_disconnected_timeout());
    radio_test_time += 29999U;
    gmk_radio_notification(0x5b, 0x33); /* A repeated notice cannot extend it. */
    assert(!gmk_radio_disconnected_timeout());
    ++radio_test_time;
    assert(gmk_radio_disconnected_timeout());
    core.connection = GMK_RADIO_UNKNOWN; /* An automatic UART retry keeps the known deadline. */
    assert(gmk_radio_disconnected_timeout());
    gmk_radio_notification(0x5b, 0x32);
    assert(!gmk_radio_disconnected_timeout());
    gmk_radio_notification(0x5b, 0x33);
    radio_test_time += 30000;
    assert(gmk_radio_disconnected_timeout());
    assert(!gmk_radio_select(3, false));
    assert(!gmk_radio_disconnected_timeout()); /* Explicit new host starts fresh. */
    run_work();
    settle();
    gmk_radio_notification(0x5b, 0x33);
    radio_test_time += 30000;
    assert(gmk_radio_disconnected_timeout());
    gmk_radio_wake(); /* An unrelated wake while not quiesced must not erase it. */
    assert(gmk_radio_disconnected_timeout());
    assert(!gmk_radio_select(0, false));
    assert(!gmk_radio_disconnected_timeout());
    start(false);
    radio_test_time = 100000;
    gmk_radio_notification(0x5b, 0x33);
    assert(!gmk_radio_disconnected_timeout());
}
static void disconnected_deadline_scheduling(void) {
#ifdef CONFIG_GMK70_EVENT_IDLE
    start(true);
    core               = (struct gmk_radio_core){.selected = 2, .active = 2, .connection = GMK_RADIO_DISCONNECTED};
    disconnected       = true;
    disconnected_since = radio_test_time = UINT32_MAX - 15000U;
    assert(gmk_radio_delay() == 30000U);
    radio_test_time += 29999U;
    assert(gmk_radio_delay() == 1);
    quiesced = true;
    assert(gmk_radio_delay() == UINT32_MAX);
    quiesced = false;
    ++radio_test_time;
    /* An expired timer need not busy-poll while USB/storage blocks sleep. */
    assert(gmk_radio_delay() == UINT32_MAX && gmk_radio_disconnected_timeout());
#endif
}
int main(void) {
    disconnected_deadline_scheduling();
    disconnected_sleep_deadline();
    wake_keeps_native_modifiers_and_is_silent_while_paused();
    slider_transition_retires_an_old_select_lease();
    slider_bypasses_usb_quiet_without_reusing_its_ack();
    unsaved_native_ids_block_binding_mutation();
#ifdef CONFIG_GMK70_NATIVE_KEYMAP_TEST
    native_keymap_remapping_tests();
    native_shortcut_taps_switch_profiles_during_an_old_select();
#endif
    nkro_submission_preserves_both_reports_and_role_gates();
    canceled_pair_after_syswork_preserves_connected_typing();
    async_selection_and_latest_request();
    mode_and_hold_work_remain_deferred();
    notifications_and_fifo_use_existing_core();
    release_after_timeout_cancels_pair_before_syswork();
    hold_pairs_once_at_three_seconds_in_system_context();
    status_waits_while_control_retry_is_being_transmitted();
    power_quiesce_requires_drained_reports_and_blocks_new_work();
    restoring_bt_slot_does_not_select_or_transmit();
#ifdef CONFIG_GMK70_CONTROLS
    host_leds_require_an_attributed_connected_host();
#endif
#ifdef CONFIG_GMK70_RADIO_QUALIFICATION
    qualification_marker_is_only_fixed_f24_and_respects_held_input();
    qualification_marker_waits_for_control_and_releases_after_failure();
#endif
    puts("Radio kernel: deferred HID, latest mailbox, hold cancellation, core FIFO and role gates passed");
    return 0;
}
