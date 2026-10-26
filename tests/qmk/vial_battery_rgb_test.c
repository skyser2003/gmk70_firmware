// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include "quantum.h"
#include "rgb_matrix.h"
#include "stock_battery.h"
#include "lowpower.h"

uint8_t             pov = 73;
bool                bat_full_flag;
static bool         cable, enabled, suspended, right;
static lpwr_state_t state;
static uint32_t     now;
static unsigned     power, flushes, native_calls;
static uint8_t      frame[77][3], shown[77][3];
void                __wrap_rgb_matrix_task(void);
bool                gmk70_vial_battery_indicator_requested(void);

bool gmk70_stock_cable_flag(void) {
    return cable;
}
uint8_t rgb_matrix_is_enabled(void) {
    return enabled;
}
bool rgb_matrix_get_suspend_state(void) {
    return suspended;
}
lpwr_state_t lpwr_get_state(void) {
    return state;
}
uint32_t timer_read32(void) {
    return now;
}
uint32_t timer_elapsed32(uint32_t previous) {
    return now - previous;
}
void __real_rgb_matrix_task(void) {
    ++native_calls;
}
void gpio_write_pin(unsigned pin, bool on) {
    assert(pin == LED_POWER_EN_PIN || pin == LED_POWER_EN2_PIN);
    if (on)
        power |= 1u << pin;
    else
        power &= ~(1u << pin);
}
void rgb_matrix_set_color(int index, uint8_t r, uint8_t g, uint8_t b) {
    assert(index >= 0 && index < 77);
    if ((index >= 36) != right) return;
    frame[index][0] = r;
    frame[index][1] = g;
    frame[index][2] = b;
}
void rgb_matrix_set_color_all(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < 77; ++i)
        rgb_matrix_set_color(i, r, g, b);
}
void rgb_matrix_update_pwm_buffers(void) {
    memcpy(shown, frame, sizeof(shown));
    ++flushes;
}
bool transaction_rpc_exec(int8_t id, uint8_t in_length, const void *in, uint8_t out_length, void *out) {
    (void)id;
    (void)in_length;
    (void)in;
    (void)out_length;
    (void)out;
    return false; // Even a failed peer send must preserve the local query.
}
static void request(uint8_t command) {
    uint8_t response;
    assert(stock_battery_receive(command, &response));
}
static void next_frame(void) {
    now += RGB_MATRIX_LED_FLUSH_LIMIT;
    __wrap_rgb_matrix_task();
    assert(!enabled); // Display queries never turn the saved animation on.
}
static void assert_black(void) {
    for (unsigned i = 0; i < sizeof(shown); ++i)
        assert(((uint8_t *)shown)[i] == 0);
}
int main(void) {
    const unsigned both  = (1u << LED_POWER_EN_PIN) | (1u << LED_POWER_EN2_PIN);
    const uint8_t  bar[] = RGB_MATRIX_BAT_INDEX_MAP;
    for (unsigned half = 0; half < 2; ++half) {
        right = half;
        memset(frame, 0, sizeof(frame));
        memset(shown, 0, sizeof(shown));
        enabled = cable = suspended = false;
        state                       = LPWR_NORMAL;
        stock_battery_key(0x7E1B, true, true);
        unsigned old_flushes = flushes;
        __wrap_rgb_matrix_task();
        assert(power == both && flushes == old_flushes + 1 && !enabled);
        for (unsigned led = 0; led < 77; ++led) {
            uint8_t green = 0;
            for (unsigned i = 0; i < 7; ++i)
                if (led == bar[i] && (led >= 36) == right) green = 150;
            assert(shown[led][0] == 0 && shown[led][1] == green && shown[led][2] == 0);
        }
        __wrap_rgb_matrix_task();
        assert(flushes == old_flushes + 1); // Respect the native flush interval.
        cable = true;
        request(0xF0);
        bat_full_flag = false;
        next_frame();
        if (!right) assert(shown[33][0] == 80 && shown[33][1] == 0);
        bat_full_flag = true;
        next_frame();
        if (!right) assert(shown[33][0] == 0 && shown[33][1] == 80);
        request(0xA5);
        next_frame();
        for (unsigned led = 0; led < 77; ++led) {
            assert(shown[led][0] == 0 && shown[led][2] == 0);
            assert(shown[led][1] == ((!right && led == 33) ? 80 : 0));
        }
        request(0xF1);
        next_frame();
        assert_black();
        assert(power == 0);

        // No charging color without a cable. Normal animations keep their task.
        cable = false;
        request(0xF0);
        assert(!gmk70_vial_battery_indicator_requested());
        old_flushes = flushes;
        next_frame();
        assert(flushes == old_flushes && power == 0);
        request(0xF1);
        request(0xA4);
        enabled             = true;
        unsigned old_native = native_calls;
        __wrap_rgb_matrix_task();
        assert(enabled && native_calls == old_native + 1 && flushes == old_flushes);
        enabled = false;
        next_frame();
        assert(power == both);

        // Existing sleep/suspend paths own LED power and must not be overridden.
        state       = LPWR_PRESLEEP;
        power       = 0;
        old_flushes = flushes;
        next_frame();
        assert(flushes == old_flushes && power == 0);
        state     = LPWR_NORMAL;
        suspended = true;
        next_frame();
        assert(flushes == old_flushes && power == 0);
        suspended = false;
        next_frame();
        assert(power == both && flushes == old_flushes + 1);
        request(0xA5);
        next_frame();
        assert_black();
        assert(power == 0);
    }
    puts("PASS: RGB-off battery/charging frames, both halves, flush limit, release clearing and sleep/suspend ownership");
}
