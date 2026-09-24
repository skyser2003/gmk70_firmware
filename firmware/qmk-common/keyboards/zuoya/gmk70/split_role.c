// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "gpio.h"

bool is_keyboard_master(void) {
    // Match stock V13: either half can process keys when plugged directly into USB.
    // Without this, the right half never executes its mapped bootloader key.
    if (gpio_read_pin(HS_BAT_CABLE_PIN)) {
        return true;
    }
    gpio_set_pin_input(SPLIT_HAND_PIN);
    return gpio_read_pin(SPLIT_HAND_PIN);
}
