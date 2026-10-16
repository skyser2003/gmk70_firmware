// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the maintained state machine with simulated time and MCU interrupts.
// These tests cannot establish which interrupt occurred on a physical keyboard.
// Reject unclassified wakes and restore the recovered V15 state transitions.
#include <assert.h>
#include <stdio.h>
#include "quantum.h"
#include "usb_main.h"
#include "wireless.h"
#include "wls/wls.h"
void md_receive_host_cb(bool resume);

typedef struct {
    uint32_t        now;
    int32_t         sync_offset;
    bool            skip_mcu_stop;
    uint32_t        last_activity;
    uint32_t        prepare_calls;
    uint32_t        power_down_calls;
    uint32_t        stop_calls;
    uint32_t        wake_calls;
    uint8_t         device;
    bool            rgb_enabled;
    bool            duplicate_request;
    bool            updater_active;
    bool            held;
    bool            exti_armed;
    bool            wake_irqs_enabled;
    bool            early_wake;
    bool            early_resume;
    uint32_t        stop_prepare_calls;
    lpwr_wakeupcd_t wake_source;
    uint8_t         requested_class;
    unsigned        assignments;
    uint32_t        usb_prepare_calls;
} sleep_fixture_t;

static sleep_fixture_t fixture;
test_usb_driver_t      USB_DRIVER;

uint32_t timer_read32(void) {
    return fixture.now;
}
uint32_t timer_elapsed32(uint32_t last) {
    return fixture.now - last;
}
uint32_t sync_timer_read32(void) {
    return fixture.now + fixture.sync_offset;
}
uint32_t sync_timer_elapsed32(uint32_t last) {
    return sync_timer_read32() - last;
}
uint32_t last_input_activity_elapsed(void) {
    return fixture.now - fixture.last_activity;
}
void last_matrix_activity_trigger(void) {
    fixture.last_activity = fixture.now;
}
uint8_t wireless_get_current_devs(void) {
    return fixture.device;
}
void chSysLock(void) {}
void chSysUnlock(void) {
    if (!fixture.exti_armed) return;
    if (fixture.early_wake) {
        fixture.early_wake = false;
        // The PAL callback records the event and disables its EXTI group.
        lpwr_set_sleep_wakeupcd(fixture.wake_source);
        fixture.wake_irqs_enabled = false;
    }
    if (fixture.early_resume) {
        fixture.early_resume = false;
        md_receive_host_cb(true);
    }
}
void chSysDisable(void) {}
void chSysEnable(void) {}
void matrix_init_pins(void) {}
void lpwr_stop_prepare(void) {
    assert(!fixture.exti_armed);
    ++fixture.stop_prepare_calls;
}
void lpwr_exti_init(void) {
    fixture.exti_armed        = true;
    fixture.wake_irqs_enabled = true;
}
bool lpwr_stop_pending(void) {
    lpwr_wakeupcd_t cause = lpwr_get_sleep_wakeupcd();
    return !fixture.wake_irqs_enabled || (cause != LPWR_WAKEUP_NONE && cause != LPWR_WAKEUP_UART);
}
// Link the actual board STOP classifiers, including USB cancellation ordering.
bool lower_sleep;
bool gmk70_stock_cable_flag(void) {
    return fixture.device == DEVS_USB;
}
void            gpio_write_pin_low(ioline_t pin) {}
static uint8_t  radio_state = MD_STATE_CONNECTED;
static unsigned radio_commands;
uint8_t        *md_getp_state(void) {
    return &radio_state;
}
void md_send_devctrl(uint8_t command) {
    ++radio_commands;
}
bool is_keyboard_master(void) {
    return true;
}
void wireless_devs_change_kb(uint8_t old_devs, uint8_t new_devs, bool reset) {}
void md_wait_for_commands(void) {}
void wait_ms(unsigned ms) {
    fixture.now += ms;
}
bool lpwr_can_enter_stop_hook(void) {
    return !fixture.skip_mcu_stop;
}
void gmk70_stock_usb_sleep_set(bool requested) {
    assert(!requested);
    if (lpwr_get_state() <= LPWR_STOP) {
        lpwr_set_state(LPWR_WAKEUP);
        lpwr_set_prepare_class(LPWR_PREPARE_ORDINARY);
    }
}
#ifndef GMK70_TEST_FRAMEWORK_WAKE
void suspend_power_down(void) {
    ++fixture.power_down_calls;
}
void suspend_wakeup_init(void) {
    ++fixture.wake_calls;
}
void suspend_wakeup_init_quantum(void) {
    ++fixture.wake_calls;
}
#endif
bool rgb_matrix_is_enabled(void) {
    return fixture.rgb_enabled;
}
void rgb_matrix_disable_noeeprom(void) {
    fixture.rgb_enabled = false;
}
void rgb_matrix_enable_noeeprom(void) {
    fixture.rgb_enabled = true;
}

// Board contract: active updates suppress timeout; PRE sends the peer sleep RPC.
bool lpwr_is_allow_timeout_hook(void) {
    return !fixture.updater_active;
}
bool lpwr_timeout_accept_hook(void) {
    return fixture.device != DEVS_USB || USB_DRIVER.state != USB_ACTIVE;
}
bool lpwr_is_allow_presleep_hook(void) {
    return !fixture.held || lpwr_get_prepare_class() != LPWR_PREPARE_ORDINARY;
}
bool lpwr_is_allow_stop_hook(void) {
    return !fixture.held || lpwr_get_prepare_class() == LPWR_PREPARE_CRITICAL || (lpwr_get_prepare_class() == LPWR_PREPARE_USB && lpwr_get_previous_state() == LPWR_PRESLEEP);
}
uint8_t lpwr_requested_classes_hook(void) {
    return fixture.requested_class;
}
void lpwr_state_changed_hook(void) {
    ++fixture.assignments;
}
void lpwr_usb_prepare_hook(void) {
    ++fixture.usb_prepare_calls;
    fixture.held = false;
}
void lpwr_presleep_hook(void) {
    assert(!fixture.rgb_enabled);
    ++fixture.prepare_calls;
}

void mcu_stop_mode(void) {
    ++fixture.stop_calls;
    lpwr_set_sleep_wakeupcd(fixture.wake_source);
    if (fixture.duplicate_request) lpwr_set_timeout_manual(true);
}
void lpwr_clock_enable(void) {
    fixture.exti_armed = false;
}

static void reset_fixture(uint8_t device, bool rgb_enabled) {
    fixture          = (sleep_fixture_t){.now = 1000, .last_activity = 1000, .device = device, .rgb_enabled = rgb_enabled};
    USB_DRIVER.state = 0;
    lpwr_set_state(LPWR_NORMAL);
    lpwr_set_prepare_class(LPWR_PREPARE_ORDINARY);
    lpwr_set_timeout_manual(false);
    lpwr_update_timestamp();
}

static void tick(uint32_t elapsed) {
    fixture.now += elapsed;
    lpwr_task();
}

static void request_sleep(void) {
    lpwr_set_timeout_manual(true);
    tick(0);
    assert(lpwr_get_state() == LPWR_PRESLEEP);
}

static void enter_stop(void) {
    request_sleep();
    tick(200);
    assert(lpwr_get_state() == LPWR_STOP);
    assert(fixture.power_down_calls == 1);
    assert(!fixture.rgb_enabled);
}

static void test_preparation_waits_for_release(void) {
    reset_fixture(DEVS_BT1, true);
    request_sleep();
    fixture.held = true;
    for (uint8_t i = 0; i < 19; ++i)
        tick(10);
    assert(fixture.prepare_calls == 0);
    assert(fixture.rgb_enabled);
    fixture.held = false;
    tick(0);
    assert(fixture.prepare_calls == 1);
    assert(lpwr_get_state() == LPWR_STOP);
    for (uint8_t i = 0; i < 10; ++i)
        tick(10);
    assert(fixture.prepare_calls == 1);
}

static void test_unclassified_interrupt_stays_asleep(void) {
    const uint8_t devices[] = {DEVS_BT1, DEVS_BT2, DEVS_BT3, DEVS_2G4};
    for (size_t device = 0; device < sizeof(devices) / sizeof(devices[0]); ++device) {
        for (uint8_t rgb = 0; rgb < 2; ++rgb) {
            reset_fixture(devices[device], rgb != 0);
            // Reproduce wireless selection with the USB power/data cable attached.
            USB_DRIVER.state = USB_ACTIVE;
            enter_stop();
            fixture.wake_source            = LPWR_WAKEUP_NONE;
            const uint32_t activity_before = fixture.last_activity;
            for (uint8_t attempt = 0; attempt < 8; ++attempt) {
                tick(200);
                assert(lpwr_get_state() == LPWR_STOP);
                assert(!fixture.rgb_enabled && fixture.wake_calls == 0);
                assert(fixture.last_activity == activity_before);
                assert(fixture.power_down_calls == 1 && fixture.prepare_calls == 1);
                assert(fixture.stop_calls == (uint32_t)attempt + 1);
            }
            // A real key wake after rejected interrupts must still restore RGB.
            fixture.wake_source = LPWR_WAKEUP_MATRIX;
            tick(200);
            assert(lpwr_get_state() == LPWR_WAKEUP);
            tick(200);
            assert(lpwr_get_state() == LPWR_NORMAL);
            assert(fixture.rgb_enabled == (rgb != 0) && fixture.wake_calls == 1);
        }
    }
}

static void test_radio_uart_stays_asleep(void) {
    reset_fixture(DEVS_2G4, true);
    enter_stop();
    fixture.wake_source = LPWR_WAKEUP_UART;
    tick(200);
    assert(lpwr_get_state() == LPWR_STOP);
    assert(!fixture.rgb_enabled && fixture.wake_calls == 0);
}

static void test_real_wakes_restore_previous_lighting(void) {
    const lpwr_wakeupcd_t sources[] = {LPWR_WAKEUP_MATRIX, LPWR_WAKEUP_CABLE, LPWR_WAKEUP_USB, LPWR_WAKEUP_ONEKEY, LPWR_WAKEUP_ENCODER, LPWR_WAKEUP_SWITCH};
    const uint8_t         devices[] = {DEVS_BT1, DEVS_BT2, DEVS_BT3, DEVS_2G4};
    for (size_t device = 0; device < sizeof(devices) / sizeof(devices[0]); ++device) {
        for (size_t source = 0; source < sizeof(sources) / sizeof(sources[0]); ++source) {
            for (uint8_t rgb = 0; rgb < 2; ++rgb) {
                reset_fixture(devices[device], rgb != 0);
                enter_stop();
                fixture.wake_source = sources[source];
                tick(200);
                assert(lpwr_get_state() == LPWR_WAKEUP);
                assert(!fixture.rgb_enabled);
                tick(199);
                assert(fixture.wake_calls == 0);
                tick(1);
                assert(lpwr_get_state() == LPWR_NORMAL);
                assert(fixture.rgb_enabled == (rgb != 0));
                assert(fixture.wake_calls == 1);
                assert(last_input_activity_elapsed() == 0);
            }
        }
    }
}

static void test_pending_request_cleared_by_state_change(void) {
    reset_fixture(DEVS_BT1, true);
    enter_stop();
    fixture.duplicate_request = true;
    fixture.wake_source       = LPWR_WAKEUP_ONEKEY;
    tick(200);
    assert(lpwr_get_state() == LPWR_WAKEUP);
    tick(200);
    assert(lpwr_get_state() == LPWR_NORMAL);
    assert(!lpwr_get_timeout_manual());
    tick(1);
    assert(lpwr_get_state() == LPWR_NORMAL);
    assert(!lpwr_get_timeout_manual());
    assert(fixture.power_down_calls == 1 && fixture.wake_calls == 1);
}

static void test_uart_wake_clears_pending_request(void) {
    reset_fixture(DEVS_2G4, true);
    enter_stop();
    fixture.duplicate_request = true;
    fixture.wake_source       = LPWR_WAKEUP_UART;
    tick(200);
    assert(lpwr_get_state() == LPWR_STOP);
    assert(!lpwr_get_timeout_manual());
}

static void test_usb_and_timeout_unchanged(void) {
    reset_fixture(DEVS_USB, true);
    USB_DRIVER.state = USB_ACTIVE;
    tick(600000);
    assert(lpwr_get_state() == LPWR_NORMAL);
    assert(fixture.rgb_enabled && fixture.prepare_calls == 0);
    lpwr_set_timeout_manual(true);
    tick(0);
    assert(lpwr_get_state() == LPWR_NORMAL && !lpwr_get_timeout_manual());

    reset_fixture(DEVS_BT1, true);
    fixture.updater_active = true;
    lpwr_set_timeout_manual(true);
    tick(600000);
    assert(lpwr_get_state() == LPWR_NORMAL && !lpwr_get_timeout_manual());

    reset_fixture(DEVS_BT1, true);
    tick(lpwr_timeout_value_read() - 1);
    assert(lpwr_get_state() == LPWR_NORMAL);
    tick(1);
    assert(lpwr_get_state() == LPWR_PRESLEEP);
}

static void test_host_suspend_in_every_phase(void) {
    for (lpwr_state_t state = LPWR_NORMAL; state <= LPWR_WAKEUP; ++state) {
        reset_fixture(DEVS_BT1, true);
        lpwr_set_state(state);
        md_receive_host_cb(false);
        assert(lpwr_get_state() == state && lpwr_get_timeout_manual());
        lpwr_set_state(state);
        assert(!lpwr_get_timeout_manual());
    }
}

static void test_repeated_resume_keeps_wake_deadline(void) {
    reset_fixture(DEVS_BT1, true);
    enter_stop();
    md_receive_host_cb(true);
    assert(lpwr_get_state() == LPWR_WAKEUP);
    tick(100);
    const uint32_t deadline = lpwr_timestamp_read();
    for (unsigned i = 0; i < 20; ++i)
        md_receive_host_cb(true);
    assert(lpwr_timestamp_read() == deadline);
    tick(100);
    assert(lpwr_get_state() == LPWR_NORMAL && fixture.wake_calls == 1);
}

static void test_stock_activity_is_separate_from_matrix_release(void) {
    reset_fixture(DEVS_BT1, true);
    tick(299999);
    // QMK updates its input activity on releases. V15's board deadline uses
    // key presses, raw HID and state/channel changes instead.
    fixture.last_activity = fixture.now;
    tick(1);
    assert(lpwr_get_state() == LPWR_PRESLEEP);
    reset_fixture(DEVS_BT1, true);
    tick(299999);
    lpwr_activity_trigger();
    tick(299999);
    assert(lpwr_get_state() == LPWR_NORMAL);
    tick(1);
    assert(lpwr_get_state() == LPWR_PRESLEEP);
}

static void test_wake_during_stop_entry(void) {
    const lpwr_wakeupcd_t sources[] = {LPWR_WAKEUP_MATRIX, LPWR_WAKEUP_ONEKEY, LPWR_WAKEUP_UART, LPWR_WAKEUP_CABLE, LPWR_WAKEUP_SWITCH};
    for (unsigned i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
        reset_fixture(DEVS_BT1, true);
        enter_stop();
        fixture.early_wake  = true;
        fixture.wake_source = sources[i];
        tick(200);
        // Going through WFI after the callback disabled a wake group can leave a half
        // unresponsive even to subsequent keys, until its power is cycled.
        assert(fixture.stop_calls == 0);
        assert(fixture.stop_prepare_calls == 1);
        assert(lpwr_get_state() == (sources[i] == LPWR_WAKEUP_UART ? LPWR_STOP : LPWR_WAKEUP));
        if (sources[i] == LPWR_WAKEUP_UART) {
            fixture.wake_source = LPWR_WAKEUP_MATRIX;
            tick(200);
            assert(lpwr_get_state() == LPWR_WAKEUP && fixture.stop_calls == 1);
        }
        tick(200);
        assert(lpwr_get_state() == LPWR_NORMAL && fixture.rgb_enabled && fixture.wake_calls == 1);
    }
}

static void test_host_resume_during_stop_entry(void) {
    reset_fixture(DEVS_BT1, true);
    enter_stop();
    fixture.early_resume = true;
    tick(200);
    assert(fixture.stop_calls == 0 && lpwr_get_state() == LPWR_WAKEUP);
    tick(200);
    assert(lpwr_get_state() == LPWR_NORMAL && fixture.rgb_enabled);
}

static void test_uart_does_not_cancel_stop(void) {
    reset_fixture(DEVS_BT1, true);
    enter_stop();
    // An unaccepted UART return must not prevent the next STOP attempt.
    fixture.wake_source = LPWR_WAKEUP_UART;
    tick(200);
    assert(fixture.stop_calls == 1 && lpwr_get_state() == LPWR_STOP);
    assert(!fixture.rgb_enabled);
}

static void test_stop_waits_for_release_and_wake_is_timed(void) {
    reset_fixture(DEVS_BT1, true);
    enter_stop();
    fixture.held = true;
    tick(1000);
    assert(fixture.stop_calls == 0 && lpwr_get_state() == LPWR_STOP);
    fixture.held        = false;
    fixture.wake_source = LPWR_WAKEUP_MATRIX;
    tick(0);
    assert(lpwr_get_state() == LPWR_WAKEUP);
    fixture.held = true;
    tick(199);
    assert(fixture.wake_calls == 0 && lpwr_get_state() == LPWR_WAKEUP);
    tick(1);
    assert(fixture.wake_calls == 1 && lpwr_get_state() == LPWR_NORMAL);
}

static void test_special_classes_and_usb_first_stop(void) {
    for (lpwr_prepare_t preparation = LPWR_PREPARE_USB; preparation <= LPWR_PREPARE_CRITICAL; ++preparation) {
        reset_fixture(DEVS_BT1, true);
        fixture.held            = true;
        fixture.requested_class = preparation;
        tick(0); // A special request does not depend on the idle/manual timeout.
        assert(lpwr_get_state() == LPWR_PRESLEEP && lpwr_get_prepare_class() == preparation);
        tick(0);
        assert(lpwr_get_state() == LPWR_STOP && lpwr_get_previous_state() == LPWR_PRESLEEP);
        tick(0);
        assert(fixture.stop_calls == (preparation == LPWR_PREPARE_USB ? 1u : 0u));
        if (preparation == LPWR_PREPARE_USB) {
            assert(fixture.usb_prepare_calls == 1);
            fixture.held = true; // A new key after the first STOP reset.
            tick(199);
            assert(fixture.stop_calls == 1 && fixture.usb_prepare_calls == 1);
            tick(1);
            assert(fixture.stop_calls == 1); // Subsequent USB STOP needs release.
            fixture.held = false;
            tick(0);
            assert(fixture.stop_calls == 2 && fixture.usb_prepare_calls == 1);
        } else {
            tick(200);
            assert(fixture.stop_calls == 1 && fixture.usb_prepare_calls == 0);
        }
    }
    reset_fixture(DEVS_BT1, true);
    unsigned before         = fixture.assignments;
    fixture.requested_class = LPWR_PREPARE_USB | LPWR_PREPARE_CRITICAL;
    request_sleep();
    assert(fixture.assignments == before + 3);
    assert(lpwr_get_prepare_class() == LPWR_PREPARE_USB && lpwr_get_previous_state() == LPWR_PRESLEEP);
}

static void test_local_deadlines_ignore_peer_clock_correction(void) {
    const int32_t offsets[] = {-61000, -1000, 241000};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        reset_fixture(DEVS_BT1, true);
        fixture.sync_offset = offsets[i];
        tick(60000);
        assert(lpwr_get_state() == LPWR_NORMAL);
        tick(240000);
        assert(lpwr_get_state() == LPWR_PRESLEEP);
        tick(0);
        fixture.wake_source = LPWR_WAKEUP_MATRIX;
        tick(100);
        assert(fixture.stop_calls == 0);
        tick(100);
        assert(lpwr_get_state() == LPWR_WAKEUP);
        tick(100);
        assert(fixture.wake_calls == 0);
        tick(100);
        assert(lpwr_get_state() == LPWR_NORMAL);
    }
}

static void test_special_rejected_wake_can_recover(void) {
    for (lpwr_prepare_t cls = LPWR_PREPARE_USB; cls <= LPWR_PREPARE_CRITICAL; ++cls) {
        reset_fixture(DEVS_BT1, true);
        fixture.requested_class = cls;
        tick(0);
        tick(0);
        fixture.wake_source = LPWR_WAKEUP_MATRIX;
        unsigned before     = fixture.assignments;
        tick(200);
        assert(lpwr_get_state() == LPWR_STOP && lpwr_get_previous_state() == LPWR_STOP);
        assert(fixture.assignments == before + 1 && fixture.stop_calls == 1);
        fixture.wake_source = LPWR_WAKEUP_CABLE;
        before              = fixture.assignments;
        tick(200);
        assert(lpwr_get_state() == LPWR_WAKEUP && lpwr_get_previous_state() == LPWR_STOP);
        assert(lpwr_get_prepare_class() == LPWR_PREPARE_ORDINARY);
        assert(fixture.assignments == before + 1 && fixture.stop_calls == 2);
        tick(200);
        assert(lpwr_get_state() == LPWR_NORMAL && fixture.rgb_enabled);
    }
}

static void test_skipped_mcu_stop_does_not_arm_wake_irqs(void) {
    reset_fixture(DEVS_BT1, true);
    enter_stop();
    fixture.skip_mcu_stop = true;
    tick(200);
    assert(fixture.stop_calls == 0 && fixture.stop_prepare_calls == 0);
    assert(lpwr_get_state() == LPWR_STOP && lpwr_get_previous_state() == LPWR_STOP);
    fixture.skip_mcu_stop = false;
    fixture.wake_source   = LPWR_WAKEUP_MATRIX;
    tick(200);
    assert(fixture.stop_calls == 1 && lpwr_get_state() == LPWR_WAKEUP);
}

static void test_wired_disconnected_radio_does_not_block(void) {
    reset_fixture(DEVS_USB, true);
    USB_DRIVER.state = USB_ACTIVE;
    radio_state      = MD_STATE_DISCONNECTED;
    radio_commands   = 0;
    fixture.held     = true;
    hs_rgb_blink_set_timer(0);
    hs_rgb_blink_hook();
    for (unsigned second = 0; second < 90; ++second) {
        fixture.now += 1000;
        last_matrix_activity_trigger();
        lpwr_activity_trigger();
        uint32_t before = fixture.now;
        hs_rgb_blink_hook();
        assert(fixture.now == before);
        assert(!lpwr_get_timeout_manual() && radio_commands == 0);
        lpwr_task();
        assert(lpwr_get_state() == LPWR_NORMAL && fixture.rgb_enabled);
    }
}

static void test_wireless_disconnected_radio_keeps_deadline(void) {
    const uint8_t devices[] = {DEVS_BT1, DEVS_BT2, DEVS_BT3, DEVS_2G4};
    for (unsigned i = 0; i < sizeof(devices) / sizeof(devices[0]); ++i) {
        reset_fixture(devices[i], true);
        radio_state    = MD_STATE_DISCONNECTED;
        radio_commands = 0;
        hs_rgb_blink_set_timer(0);
        hs_rgb_blink_hook();
        fixture.now += HS_LBACK_TIMEOUT - 1;
        hs_rgb_blink_hook();
        assert(!lpwr_get_timeout_manual() && radio_commands == 0);
        ++fixture.now;
        uint32_t before = fixture.now;
        hs_rgb_blink_hook();
        assert(fixture.now == before + 200);
        assert(lpwr_get_timeout_manual() && radio_commands == 1);
        lpwr_task();
        assert(lpwr_get_state() == LPWR_PRESLEEP);
    }
}

static void test_usb_cancels_wireless_recovery_deadline(void) {
    reset_fixture(DEVS_BT1, true);
    radio_state    = MD_STATE_DISCONNECTED;
    radio_commands = 0;
    hs_rgb_blink_set_timer(0);
    hs_rgb_blink_hook();
    fixture.now += 15000;
    hs_rgb_blink_hook();
    fixture.device = DEVS_USB;
    hs_rgb_blink_hook();
    fixture.now += 45000;
    uint32_t before = fixture.now;
    hs_rgb_blink_hook();
    assert(fixture.now == before && !lpwr_get_timeout_manual());
    fixture.device = DEVS_BT1;
    hs_rgb_blink_hook();
    assert(fixture.now == before && radio_commands == 0);
    fixture.now += HS_LBACK_TIMEOUT - 1;
    hs_rgb_blink_hook();
    assert(!lpwr_get_timeout_manual());
    ++fixture.now;
    hs_rgb_blink_hook();
    assert(lpwr_get_timeout_manual() && radio_commands == 1);
}

typedef struct {
    const char *name;
    void (*run)(void);
} sleep_test_t;

int main(int argc, char **argv) {
    reset_fixture(DEVS_BT1, true);
    fixture.now = 0;
    lpwr_set_state(LPWR_NORMAL);
    lpwr_set_timeout_manual(true);
    tick(1000);
    assert(lpwr_get_state() == LPWR_NORMAL && lpwr_get_timeout_manual());
    tick(0);
    assert(lpwr_get_state() == LPWR_PRESLEEP);
    const sleep_test_t tests[] = {
        {"local-clock", test_local_deadlines_ignore_peer_clock_correction},
        {"special-recovery", test_special_rejected_wake_can_recover},
        {"skip-mcu-stop", test_skipped_mcu_stop_does_not_arm_wake_irqs},
        {"host-suspend-phases", test_host_suspend_in_every_phase},
        {"held-stop-wake", test_stop_waits_for_release_and_wake_is_timed},
        {"special-classes", test_special_classes_and_usb_first_stop},
        {"held-preparation", test_preparation_waits_for_release},
        {"unclassified-interrupt", test_unclassified_interrupt_stays_asleep},
        {"radio-uart", test_radio_uart_stays_asleep},
        {"real-wakes", test_real_wakes_restore_previous_lighting},
        {"pending-request", test_pending_request_cleared_by_state_change},
        {"uart-request", test_uart_wake_clears_pending_request},
        {"usb-and-timeout", test_usb_and_timeout_unchanged},
        {"resume-deadline", test_repeated_resume_keeps_wake_deadline},
        {"activity-deadline", test_stock_activity_is_separate_from_matrix_release},
        {"wake-before-stop", test_wake_during_stop_entry},
        {"host-before-stop", test_host_resume_during_stop_entry},
        {"prepared-uart", test_uart_does_not_cancel_stop},
        {"wired-radio-backoff", test_wired_disconnected_radio_does_not_block},
        {"wireless-radio-backoff", test_wireless_disconnected_radio_keeps_deadline},
        {"radio-backoff-mode-change", test_usb_cancels_wireless_recovery_deadline},
    };
    bool ran = false;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (argc > 1 && strcmp(argv[1], tests[i].name) != 0) continue;
        tests[i].run();
        printf("PASS: %s\n", tests[i].name);
        ran = true;
    }
    return ran ? 0 : 1;
}
