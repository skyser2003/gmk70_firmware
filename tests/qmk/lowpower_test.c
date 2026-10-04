// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the maintained state machine with simulated time and MCU interrupts.
// These tests cannot establish which interrupt occurred on a physical keyboard.
// Reject unclassified wakes as observed in stock V13/V15 and the October 3
// device capture. Preserve the other public-port transitions and timing.
#include <assert.h>
#include <stdio.h>
#include "quantum.h"
#include "usb_main.h"
#include "wireless.h"

typedef struct {
    uint32_t        now;
    uint32_t        last_activity;
    uint32_t        prepare_calls;
    uint32_t        power_down_calls;
    uint32_t        stop_calls;
    uint32_t        wake_calls;
    uint8_t         device;
    bool            rgb_enabled;
    bool            duplicate_request;
    lpwr_wakeupcd_t wake_source;
} sleep_fixture_t;

static sleep_fixture_t fixture;
test_usb_driver_t      USB_DRIVER;

uint32_t sync_timer_read32(void) {
    return fixture.now;
}
uint32_t sync_timer_elapsed32(uint32_t last) {
    return fixture.now - last;
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
void chSysUnlock(void) {}
void chSysDisable(void) {}
void chSysEnable(void) {}
void matrix_init_pins(void) {}
void suspend_power_down(void) {
    ++fixture.power_down_calls;
}
void suspend_wakeup_init(void) {
    ++fixture.wake_calls;
}
bool rgb_matrix_is_enabled(void) {
    return fixture.rgb_enabled;
}
void rgb_matrix_disable_noeeprom(void) {
    fixture.rgb_enabled = false;
}
void rgb_matrix_enable_noeeprom(void) {
    fixture.rgb_enabled = true;
}

// Board contract: USB suppresses timeout; preparation sends the peer sleep RPC.
bool lpwr_is_allow_timeout_hook(void) {
    return fixture.device != DEVS_USB;
}
bool lpwr_is_allow_presleep_hook(void) {
    ++fixture.prepare_calls;
    return true;
}

void mcu_stop_mode(void) {
    ++fixture.stop_calls;
    lpwr_set_sleep_wakeupcd(fixture.wake_source);
    if (fixture.duplicate_request) lpwr_set_timeout_manual(true);
}

static void reset_fixture(uint8_t device, bool rgb_enabled) {
    fixture          = (sleep_fixture_t){.now = 1000, .last_activity = 1000, .device = device, .rgb_enabled = rgb_enabled};
    USB_DRIVER.state = 0;
    lpwr_set_state(LPWR_NORMAL);
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

static void test_preparation_keeps_baseline_timing(void) {
    reset_fixture(DEVS_BT1, true);
    request_sleep();
    for (uint8_t i = 0; i < 19; ++i)
        tick(10);
    assert(fixture.prepare_calls == 19);
    assert(fixture.rgb_enabled);
    tick(10);
    assert(fixture.prepare_calls == 20);
    assert(lpwr_get_state() == LPWR_STOP);
    for (uint8_t i = 0; i < 10; ++i)
        tick(10);
    assert(fixture.prepare_calls == 20);
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

static void test_pending_request_keeps_baseline_lifetime(void) {
    reset_fixture(DEVS_BT1, true);
    enter_stop();
    fixture.duplicate_request = true;
    fixture.wake_source       = LPWR_WAKEUP_ONEKEY;
    tick(200);
    assert(lpwr_get_state() == LPWR_WAKEUP);
    tick(200);
    assert(lpwr_get_state() == LPWR_NORMAL);
    assert(lpwr_get_timeout_manual());
    tick(1);
    assert(lpwr_get_state() == LPWR_PRESLEEP);
    assert(!lpwr_get_timeout_manual());
    assert(fixture.power_down_calls == 1 && fixture.wake_calls == 1);
}

static void test_uart_wake_preserves_pending_request(void) {
    reset_fixture(DEVS_2G4, true);
    enter_stop();
    fixture.duplicate_request = true;
    fixture.wake_source       = LPWR_WAKEUP_UART;
    tick(200);
    assert(lpwr_get_state() == LPWR_STOP);
    assert(lpwr_get_timeout_manual());
}

static void test_usb_and_timeout_unchanged(void) {
    reset_fixture(DEVS_USB, true);
    USB_DRIVER.state = USB_ACTIVE;
    lpwr_set_timeout_manual(true);
    tick(600000);
    assert(lpwr_get_state() == LPWR_NORMAL);
    assert(fixture.rgb_enabled && fixture.prepare_calls == 0);
    assert(!lpwr_get_timeout_manual());

    reset_fixture(DEVS_BT1, true);
    tick(lpwr_timeout_value_read() - 1);
    assert(lpwr_get_state() == LPWR_NORMAL);
    tick(1);
    assert(lpwr_get_state() == LPWR_PRESLEEP);
}

typedef struct {
    const char *name;
    void (*run)(void);
} sleep_test_t;

int main(int argc, char **argv) {
    const sleep_test_t tests[] = {
        {"baseline-preparation", test_preparation_keeps_baseline_timing}, {"unclassified-interrupt", test_unclassified_interrupt_stays_asleep}, {"radio-uart", test_radio_uart_stays_asleep}, {"real-wakes", test_real_wakes_restore_previous_lighting}, {"baseline-pending-request", test_pending_request_keeps_baseline_lifetime}, {"baseline-uart-request", test_uart_wake_preserves_pending_request}, {"usb-and-timeout", test_usb_and_timeout_unchanged},
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
