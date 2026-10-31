/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../../firmware/zmk/studio/bool_setting.h"

uint32_t radio_test_time;
unsigned radio_test_locked;

int main(void) {
    struct gmk_bool_setting setting;
    gmk_bool_setting_init(&setting, "runtime/state", true);
    radio_test_time = 1000;
    gmk_bool_setting_poll(&setting, true);
    assert(!setting.work.pending && gmk_bool_setting_get(&setting));
    assert(!gmk_bool_setting_toggle(&setting));
    radio_test_time = 1250;
    gmk_bool_setting_poll(&setting, true);
    assert(setting.work.pending);
    setting.work.pending = false;
    setting.work.handler(&setting.work);
    assert(!gmk_bool_setting_get(&setting));
    radio_test_time = 2250;
    gmk_bool_setting_poll(&setting, true);
    assert(setting.work.pending);
    puts("Boolean settings: runtime mode works without a settings backend");
}
