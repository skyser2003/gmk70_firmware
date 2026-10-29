/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zmk/events/keycode_state_changed.h>
#include "../../firmware/zmk/radio/radio.c"

uint32_t                              radio_test_time;
unsigned                              radio_test_locked;
static uint8_t                        physical_raw = 3;
static bool                           left         = true, in_work;
static unsigned                       clears, events;
static bool                           inject;
static bool                           inject_release;
static struct zmk_hid_keyboard_report keyboard;
static struct zmk_hid_consumer_report consumer;
static uint8_t                        explicit_mods;
bool                                  gmk_wired_is_left(void) {
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
int __real_zmk_keymap_set_layer_binding_at_idx(zmk_keymap_layer_id_t layer, uint16_t index, const struct zmk_behavior_binding binding) {
    (void)layer;
    (void)index;
    (void)binding;
    assert(!radio_test_locked);
    return 17;
}
int raise_zmk_keycode_state_changed(struct zmk_keycode_state_changed event) {
    assert(in_work && !radio_test_locked && event.usage_page == 7 && event.keycode == 0x73);
    assert(!event.implicit_modifiers && !event.explicit_modifiers);
    assert(event.state == (events % 2 == 0));
    ++events;
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
    assert(__wrap_zmk_keymap_set_layer_binding_at_idx(0, 1, binding) == 17);
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
            assert(__wrap_zmk_keymap_set_layer_binding_at_idx(1, 9, replacement) == 17);
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
#endif
int main(void) {
    canceled_pair_after_syswork_preserves_connected_typing();
    async_selection_and_latest_request();
    mode_and_hold_work_remain_deferred();
    notifications_and_fifo_use_existing_core();
    release_after_timeout_cancels_pair_before_syswork();
    hold_pairs_once_at_three_seconds_in_system_context();
    status_waits_while_control_retry_is_being_transmitted();
#ifdef CONFIG_GMK70_RADIO_QUALIFICATION
    qualification_marker_is_only_fixed_f24_and_respects_held_input();
#endif
    puts("Radio kernel: deferred HID, latest mailbox, hold cancellation, core FIFO and role gates passed");
    return 0;
}
