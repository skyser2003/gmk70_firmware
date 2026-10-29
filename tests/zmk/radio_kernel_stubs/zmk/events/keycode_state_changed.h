/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
struct zmk_keycode_state_changed {
    uint16_t usage_page;
    uint32_t keycode;
    uint8_t  implicit_modifiers, explicit_modifiers;
    bool     state;
    int64_t  timestamp;
};
int raise_zmk_keycode_state_changed(struct zmk_keycode_state_changed event);
