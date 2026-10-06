// SPDX-License-Identifier: GPL-2.0-or-later
#include "stock_power.h"

static uint8_t held_keys;

void stock_power_key(bool pressed) {
    // V15 0x080041BC: saturating four-key counter, including non-basic keys.
    if (pressed) {
        if (held_keys < 4) ++held_keys;
    } else if (held_keys) {
        --held_keys;
    }
}

bool stock_power_prepare_allowed(bool critical_battery) {
    // V15 0x08002838/0x08002864; forced low-battery class bypasses held keys.
    return critical_battery || held_keys == 0;
}

uint32_t stock_power_timeout(uint8_t selector) {
    static const uint32_t durations[8] = {0, 60000, 180000, 300000, 600000, 1200000, 1800000, 300000};
    return durations[selector & 7];
}
