// SPDX-License-Identifier: GPL-2.0-or-later
// Real low-power task, framework resume, quantum resume and board callbacks.
// Only MCU/host/report storage is simulated; no Bluetooth device is accessed.
#define main lowpower_fixture_tests
#include "lowpower_test.c"
#undef main
#include "wake_input_fixture.h"

wake_board_config_t confinfo;
static uint8_t      mods, weak_mods, physical_mods, physical_device;
static bool         keys[256], physical_keys[256], rgb_suspended;
static unsigned     clears, led_wakes, module_wakes, uart_restores, led_power, selections;
static uint16_t     consumer, system_usage;
static bool         mouse_button, programmable_button;
typedef struct {
    uint8_t mods;
    bool    tab, a;
} input_report_t;
static input_report_t report;

void clear_mods(void) {
    mods = 0;
    ++clears;
}
void clear_weak_mods(void) {
    weak_mods = 0;
}
void clear_keys(void) {
    memset(keys, 0, sizeof(keys));
}
void mousekey_clear(void) {
    mouse_button = false;
}
void programmable_button_clear(void) {
    programmable_button = false;
}
void host_system_send(uint16_t usage) {
    system_usage = usage;
}
void host_consumer_send(uint16_t usage) {
    consumer = usage;
}
void led_wakeup(void) {
    ++led_wakes;
    ++fixture.wake_calls;
}
void rgb_matrix_set_suspend_state(bool suspended) {
    rgb_suspended = suspended;
}
void suspend_wakeup_init_modules(void) {
    ++module_wakes;
}
void suspend_power_down_quantum(void) {
    ++fixture.power_down_calls;
    rgb_suspended = true;
}
void usart_init(void) {
    ++uart_restores;
}
void gpio_set_pin_output(ioline_t pin) {
    assert(!"master must not pulse split TX");
}
void wait_us(uint16_t us) {
    assert(!"master must not pulse split TX");
}
void gpio_write_pin_high(ioline_t pin) {
    assert(pin == A5 || pin == A8);
    ++led_power;
}
bool gpio_read_pin(ioline_t pin) {
    assert(pin == C14 || pin == C15);
    return physical_device == DEVS_USB || (pin == C14 && physical_device != DEVS_2G4) || (pin == C15 && physical_device == DEVS_2G4);
}
void wireless_devs_change(uint8_t old_devs, uint8_t new_devs, bool reset) {
    ++selections;
    fixture.device = confinfo.devs = new_devs;
    lpwr_activity_trigger();
    if (lpwr_get_state() == LPWR_PRESLEEP || lpwr_get_state() == LPWR_STOP) {
        lpwr_set_state(LPWR_WAKEUP);
        lpwr_set_prepare_class(LPWR_PREPARE_ORDINARY);
    }
}
static void send_report(void) {
    report = (input_report_t){.mods = mods | weak_mods, .tab = keys[0x2B], .a = keys[4]};
}
static void mod_edge(uint8_t mask, bool pressed) {
    // Like matrix_task(), unchanged held keys do not generate another press.
    if (((physical_mods & mask) != 0) == pressed) return;
    if (pressed) {
        physical_mods |= mask;
        mods |= mask;
    } else {
        physical_mods &= ~mask;
        mods &= ~mask;
    }
    send_report();
}
static void key_edge(uint8_t key, bool pressed) {
    if (physical_keys[key] == pressed) return;
    physical_keys[key] = keys[key] = pressed;
    send_report();
}
static void reset_input(uint8_t device) {
    reset_fixture(device, true);
    mods = weak_mods = physical_mods = 0;
    memset(keys, 0, sizeof(keys));
    memset(physical_keys, 0, sizeof(physical_keys));
    physical_device = device;
    confinfo        = (wake_board_config_t){.devs = device, .last_btdevs = DEVS_BT1};
    rgb_suspended = mouse_button = programmable_button = false;
    clears = led_wakes = module_wakes = uart_restores = led_power = selections = 0;
    consumer = system_usage = 0;
    send_report();
}
static void wireless_wake(uint8_t device) {
    reset_input(device);
    enter_stop();
    fixture.wake_source = LPWR_WAKEUP_MATRIX;
    tick(200);
    assert(lpwr_get_state() == LPWR_WAKEUP);
}
static void test_alt_tab_after_idle(void) {
    for (uint8_t dev = DEVS_BT1; dev <= DEVS_2G4; ++dev) {
        wireless_wake(dev);
        mod_edge(0x04, true);
        key_edge(0x2B, true);
        assert(report.mods == 0x04 && report.tab);
        key_edge(0x2B, false);
        tick(199);
        assert(mods == 0x04 && physical_mods == 0x04);
        tick(1);
        assert(lpwr_get_state() == LPWR_NORMAL);
        // A later Tab report must not commit Windows' selection while Alt is held.
        key_edge(0x2B, true);
        assert(physical_mods == 0x04 && report.mods == 0x04 && report.tab);
        assert(clears == 0 && fixture.wake_calls == 1);
        assert(led_wakes && module_wakes && uart_restores && led_power && selections);
        assert(fixture.rgb_enabled && !rgb_suspended);
        key_edge(0x2B, false);
        mod_edge(0x04, false);
        assert(report.mods == 0 && !report.tab);
    }
}
static void test_held_keys_and_weak_mods(void) {
    wireless_wake(DEVS_BT1);
    weak_mods    = 0x02;
    mouse_button = programmable_button = true;
    consumer                           = 0xE9;
    system_usage                       = 0x81;
    mod_edge(0x04, true);
    key_edge(4, true);
    tick(200);
    key_edge(0x2B, true);
    assert(report.mods == 0x06 && report.a && report.tab);
    assert(mouse_button && programmable_button && consumer == 0xE9 && system_usage == 0x81);
    key_edge(4, false);
    key_edge(0x2B, false);
    mod_edge(0x04, false);
    weak_mods = 0;
    send_report();
    assert(report.mods == 0 && !report.a && !report.tab);
}
static void test_release_before_wake_completion(void) {
    wireless_wake(DEVS_BT1);
    mod_edge(0x04, true);
    key_edge(0x2B, true);
    tick(199);
    key_edge(0x2B, false);
    mod_edge(0x04, false);
    tick(1);
    send_report();
    assert(report.mods == 0 && !report.tab && physical_mods == 0);
}
static void test_native_usb_resume(void) {
    reset_input(DEVS_USB);
    USB_DRIVER.state = USB_ACTIVE;
    lpwr_set_state(LPWR_STOP);
    mods = weak_mods = 0xFF;
    keys[4]          = true;
    mouse_button = programmable_button = true;
    consumer = system_usage = 1;
    suspend_wakeup_init(); // Unmodified framework native USB resume.
    assert(clears == 1 && mods == 0 && weak_mods == 0 && !keys[4]);
    assert(!mouse_button && !programmable_button && !consumer && !system_usage);
    assert(lpwr_get_state() == LPWR_WAKEUP);
    mod_edge(0x04, true);
    key_edge(0x2B, true);
    tick(200);
    key_edge(0x2B, false);
    assert(report.mods == 0x04 && clears == 1);
    assert(fixture.wake_calls == 2);
    mod_edge(0x04, false);
    assert(report.mods == 0);
}
static void test_normal_usb_control(void) {
    reset_input(DEVS_USB);
    USB_DRIVER.state = USB_ACTIVE;
    mod_edge(0x04, true);
    tick(250);
    key_edge(0x2B, true);
    assert(report.mods == 0x04 && clears == 0 && lpwr_get_state() == LPWR_NORMAL);
}
static void test_physical_host_switch_clears_input(void) {
    reset_input(DEVS_BT1);
    mod_edge(0x04, true);
    key_edge(0x2B, true);
    physical_device = DEVS_USB;
    assert(hs_mode_scan(true, DEVS_BT1, DEVS_BT1));
    assert(mods == 0 && !keys[0x2B] && clears == 1);
    assert(fixture.device == DEVS_USB && fixture.wake_calls == 1);
}
int main(int argc, char **argv) {
    const sleep_test_t tests[] = {
        {"alt-tab", test_alt_tab_after_idle}, {"held-input", test_held_keys_and_weak_mods}, {"released-input", test_release_before_wake_completion}, {"usb-resume", test_native_usb_resume}, {"usb-control", test_normal_usb_control}, {"host-switch", test_physical_host_switch_clears_input},
    };
    bool ran = false;
    for (size_t i = 0; i < sizeof(tests) / sizeof(*tests); ++i) {
        if (argc > 1 && strcmp(argv[1], tests[i].name) != 0) continue;
        tests[i].run();
        printf("PASS: %s\n", tests[i].name);
        ran = true;
    }
    return ran ? 0 : 1;
}
