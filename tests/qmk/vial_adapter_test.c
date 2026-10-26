// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include "quantum.h"
#include "eeprom.h"
#include "stock_v15_fixture.h"
#include "wireless.h"
#include "stock_power.h"
#include "lowpower.h"

static bool         native_enabled;
static uint32_t     marker;
static unsigned     writes, forwarded, replies;
static unsigned     key_activity, wired_wake, user_calls;
static uint8_t      device       = DEVS_BT1;
static bool         user_accepts = true;
static bool         rgb_enabled, rgb_suspended, battery_requested;
static unsigned     led_pins, pin_writes, housekeeping_calls;
static lpwr_state_t power_state = LPWR_NORMAL;
uint8_t             rgb_matrix_is_enabled(void) {
    return rgb_enabled;
}
bool rgb_matrix_get_suspend_state(void) {
    return rgb_suspended;
}
bool gmk70_vial_battery_indicator_requested(void) {
    return battery_requested;
}
lpwr_state_t lpwr_get_state(void) {
    return power_state;
}
void gpio_write_pin(unsigned pin, bool enabled) {
    assert(pin == LED_POWER_EN_PIN || pin == LED_POWER_EN2_PIN);
    if (enabled)
        led_pins |= 1u << pin;
    else
        led_pins &= ~(1u << pin);
    ++pin_writes;
}
void __real_housekeeping_task(void) {
    ++housekeeping_calls;
}
void    __wrap_housekeeping_task(void);
uint8_t wireless_get_current_devs(void) {
    return device;
}
void lpwr_activity_trigger(void) {
    ++key_activity;
}
void usb_remote_host(void) {
    ++wired_wake;
}
bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
    ++user_calls;
    return user_accepts;
}
bool pre_process_record_kb(uint16_t keycode, keyrecord_t *record);
bool __real_nvm_eeconfig_is_enabled(void) {
    return native_enabled;
}
void __real_nvm_eeconfig_enable(void) {
    native_enabled = true;
}
uint32_t eeprom_read_dword(const uint32_t *address) {
    assert((uintptr_t)address == 0xFFC);
    return marker;
}
void eeprom_update_dword(uint32_t *address, uint32_t value) {
    assert((uintptr_t)address == 0xFFC);
    marker = value;
    ++writes;
}
void __real_raw_hid_receive(uint8_t *data, uint8_t length) {
    assert(length == 32);
    ++forwarded;
}
void raw_hid_send(uint8_t *data, uint8_t length) {
    assert(length == 32);
    ++replies;
}
bool __wrap_nvm_eeconfig_is_enabled(void);
void __wrap_nvm_eeconfig_enable(void);
void __wrap_raw_hid_receive(uint8_t *data, uint8_t length);

int main(void) {
    // A valid stock core word cannot validate a native Vial storage layout.
    native_enabled                   = true;
    const uint32_t invalid_markers[] = {0, 0xFFFFFFFF, 0x19082600, 0x564D4700, 0x564D4701};
    for (unsigned i = 0; i < sizeof(invalid_markers) / sizeof(invalid_markers[0]); ++i) {
        marker = invalid_markers[i];
        assert(!__wrap_nvm_eeconfig_is_enabled() && writes == 0);
    }
    __wrap_nvm_eeconfig_enable();
    assert(__wrap_nvm_eeconfig_is_enabled() && writes == 1);
    assert(marker == 0x564D4702);
    native_enabled = false;
    assert(!__wrap_nvm_eeconfig_is_enabled());
    // Identification, bootloader, reset and every Vial unlock operation must
    // go through the fork's real handler and its permission checks.
    for (unsigned command = 0; command < 256; ++command) {
        uint8_t  data[32] = {command, 0xA5, 0xA5};
        unsigned before   = forwarded;
        __wrap_raw_hid_receive(data, 32);
        assert(forwarded == before + (command != 0x21));
    }
    for (unsigned command = 0; command <= 13; ++command) {
        uint8_t  data[32] = {0xFE, command};
        unsigned before   = forwarded;
        __wrap_raw_hid_receive(data, 32);
        assert(forwarded == before + 1);
    }
    for (unsigned selector = 0; selector < 8; ++selector) {
        uint8_t  data[32] = {7, 0, 1, selector};
        unsigned before   = forwarded;
        __wrap_raw_hid_receive(data, 32);
        assert(stock_v15_fixture.sleep == selector && forwarded == before);
        data[0] = 8;
        data[3] = 0xFF;
        __wrap_raw_hid_receive(data, 32);
        assert(data[3] == selector && forwarded == before);
    }
    uint8_t data[32] = {0x21, 1, 1};
    __wrap_raw_hid_receive(data, 32);
    data[1] = 0;
    __wrap_raw_hid_receive(data, 32);
    assert(data[1] == 1);
    const unsigned activity = stock_v15_fixture.activity, before = forwarded;
    for (unsigned length = 0; length < 32; ++length)
        __wrap_raw_hid_receive(data, length);
    assert(stock_v15_fixture.activity == activity && forwarded == before);
    assert(replies == 19 && writes == 1);

    // A physical combo/repeat trigger can be consumed before process_record_kb.
    // Both halves' presses must still refresh activity and prevent idle sleep.
    for (unsigned row = 0; row < MATRIX_ROWS; ++row) {
        stock_power_reset_keys();
        keyrecord_t key             = {.event = {.key = {.row = row, .col = 1}, .type = KEY_EVENT, .pressed = true}};
        unsigned    activity_before = key_activity;
        user_accepts                = false;
        assert(!pre_process_record_kb(4, &key));
        assert(key_activity == activity_before + 1 && !stock_power_prepare_allowed(false));
        // Vial's virtual macro position, combo output and encoder events cannot
        // decrement the held physical key count, even with unbalanced releases.
        const keyevent_t synthetic[] = {
            {.key = {.row = 240, .col = 240}, .type = KEY_EVENT}, {.type = COMBO_EVENT}, {.type = ENCODER_CW_EVENT}, {.type = ENCODER_CCW_EVENT}, {.key = {.row = MATRIX_ROWS, .col = 0}, .type = KEY_EVENT}, {.key = {.row = 0, .col = MATRIX_COLS}, .type = KEY_EVENT},
        };
        for (unsigned i = 0; i < sizeof(synthetic) / sizeof(synthetic[0]); ++i) {
            keyrecord_t output = {.event = synthetic[i]};
            pre_process_record_kb(4, &output);
            assert(!stock_power_prepare_allowed(false) && key_activity == activity_before + 1);
        }
        key.event.pressed = false;
        pre_process_record_kb(4, &key);
        assert(stock_power_prepare_allowed(false) && key_activity == activity_before + 1);
    }
    user_accepts        = true;
    device              = DEVS_USB;
    keyrecord_t encoder = {.event = {.type = ENCODER_CW_EVENT, .pressed = true}};
    assert(pre_process_record_kb(4, &encoder));
    assert(wired_wake == 1 && stock_power_prepare_allowed(false));
    keyrecord_t    tick            = {.event = {.type = TICK_EVENT, .pressed = true}};
    const unsigned activity_before = key_activity, calls_before = user_calls;
    assert(pre_process_record_kb(4, &tick));
    assert(wired_wake == 1 && key_activity == activity_before && user_calls == calls_before + 1);

    // The slave can boot with RGB disabled in its local EEPROM. Native split
    // sync replaces that RAM setting with the enabled master setting, without
    // calling the board's physical RGB Toggle handler or changing LED power.
    const unsigned both_pins              = (1u << LED_POWER_EN_PIN) | (1u << LED_POWER_EN2_PIN);
    const unsigned writes_before_lighting = writes;
    rgb_enabled                           = true;
    assert(led_pins == 0);
    __wrap_housekeeping_task();
    assert(led_pins == both_pins && housekeeping_calls == 1);

    // Native VialRGB Off/On also bypasses the stock toggle RPC. Hardware power
    // must follow the resulting config on both halves, with no EEPROM writes.
    rgb_enabled = false;
    __wrap_housekeeping_task();
    assert(led_pins == 0);
    rgb_enabled = true;
    __wrap_housekeeping_task();
    assert(led_pins == both_pins);
    rgb_suspended = true;
    __wrap_housekeeping_task();
    assert(led_pins == 0);
    rgb_suspended = false;
    __wrap_housekeeping_task();
    assert(led_pins == both_pins);
    assert(writes == writes_before_lighting);

    // Sleep preparation and recovery own the pins outside NORMAL. A master's
    // still-enabled config must not undo the slave's STOP/critical power-down.
    const unsigned pin_writes_before_sleep = pin_writes;
    led_pins                               = 0;
    for (power_state = LPWR_PRESLEEP; power_state <= LPWR_WAKEUP; ++power_state) {
        __wrap_housekeeping_task();
        assert(led_pins == 0 && pin_writes == pin_writes_before_sleep);
    }
    power_state = LPWR_NORMAL;
    __wrap_housekeeping_task();
    assert(led_pins == both_pins && housekeeping_calls == 9);

    // A held battery query needs LED power while the saved RGB state is off.
    rgb_enabled       = false;
    battery_requested = true;
    __wrap_housekeeping_task();
    assert(led_pins == both_pins && !rgb_enabled);
    battery_requested = false;
    __wrap_housekeeping_task();
    assert(led_pins == 0 && !rgb_enabled);
    puts("PASS: Vial schema isolation, native protocol/unlock routing, packet bounds and physical activity before consumed/synthetic events");
    puts("PASS: Vial split RGB power recovery, native Off/On, suspend and sleep ownership without EEPROM writes");
    return 0;
}
