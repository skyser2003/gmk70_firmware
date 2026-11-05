/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../dependencies/zmk/app/src/hid_indicators.c"

void controls_native_led_work_run(void) {
    assert(led_changed_work.pending);
    led_changed_work.pending = false;
    led_changed_work.handler(&led_changed_work);
}
