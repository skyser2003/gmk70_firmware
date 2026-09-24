// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Stable VIA ABI: do not compact unused slots or derive this order from labels.
// Slots 7, 9, and 26-33 were checked against the stock V13 dispatch table.
enum gmk70_stock_keycodes {
    KC_BT1  = 0x7E00,
    KC_BT2  = 0x7E01,
    KC_BT3  = 0x7E02,
    KC_BT4  = 0x7E03,
    KC_BT5  = 0x7E04,
    KC_2G4  = 0x7E05,
    KC_USB  = 0x7E06,
    KC_BATQ = 0x7E07,
    RL_MOD  = 0x7E09,
    KC_FILP = 0x7E1A,
    HS_BATQ = 0x7E1B,
    MOR_1   = 0x7E1C,
    MOR_2   = 0x7E1D,
    MOR_3   = 0x7E1E,
    MOR_4   = 0x7E1F,
    MOR_5   = 0x7E20,
    MOR_6   = 0x7E21,
};

uint8_t  gmk70_wireless_device(uint16_t keycode);
bool     gmk70_device_allowed(uint8_t device, uint8_t physical_mode);
uint16_t gmk70_shortcut_keycode(uint16_t keycode);

uint16_t gmk70_rgb_keycode(uint16_t keycode);
