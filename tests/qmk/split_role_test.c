// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include "quantum.h"
#include "gpio.h"

static bool usb_present;
static bool left_half;
static bool hand_pin_configured;

void gpio_set_pin_input(pin_t pin) {
    assert(pin == HS_BAT_CABLE_PIN || pin == SPLIT_HAND_PIN);
    if (pin == SPLIT_HAND_PIN) {
        hand_pin_configured = true;
    }
}

bool gpio_read_pin(pin_t pin) {
    if (pin == HS_BAT_CABLE_PIN) {
        return usb_present;
    }
    assert(pin == SPLIT_HAND_PIN);
    assert(hand_pin_configured);
    return left_half;
}

typedef struct {
    bool usb;
    bool left;
    bool master;
} role_case_t;

int main(void) {
    // Stock V13 is_keyboard_master at 0x08004BEC checks USB (A7) before hand (B9).
    const role_case_t cases[] = {
        {false, false, false}, // Right slave on the inter-half cable.
        {false, true, true},   // Left master when operating wirelessly.
        {true, false, true},   // Right half connected directly by USB for configuration/recovery.
        {true, true, true},    // Left master on USB.
        {false, false, false}, // Role follows cable removal rather than remaining latched.
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        usb_present         = cases[index].usb;
        left_half           = cases[index].left;
        hand_pin_configured = false;
        if (is_keyboard_master() != cases[index].master) {
            fprintf(stderr, "Split-role mismatch: USB=%d, left=%d\n", usb_present, left_half);
            return 1;
        }
    }
    puts("GMK70 split roles match stock V13, including right-half USB recovery");
    return 0;
}
