// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include "quantum.h"
#include "wireless.h"
#include "stock_power.h"

static unsigned key_activity, wired_wake, user_calls;
static uint8_t  device       = DEVS_BT1;
static bool     user_accepts = true;

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
    (void)keycode;
    (void)record;
    ++user_calls;
    return user_accepts;
}
bool pre_process_record_kb(uint16_t keycode, keyrecord_t *record);

int main(void) {
    // Consumed physical triggers still prevent sleep on either matrix half.
    for (unsigned row = 0; row < MATRIX_ROWS; ++row) {
        stock_power_reset_keys();
        keyrecord_t key             = {.event = {.key = {.row = row, .col = 1}, .type = KEY_EVENT, .pressed = true}};
        unsigned    activity_before = key_activity;
        user_accepts                = false;
        assert(!pre_process_record_kb(4, &key));
        assert(key_activity == activity_before + 1 && !stock_power_prepare_allowed(false));
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
    puts("PASS: shared physical activity before consumed/synthetic events and wired wake");
}
