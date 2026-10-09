// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#define VIAL_KEYBOARD_UID {0xB6, 0x91, 0xD7, 0x27, 0x7F, 0xC8, 0x82, 0x51}
// Escape and left Shift, on the left half, held during Vial unlock.
#define VIAL_UNLOCK_COMBO_ROWS {0, 3}
#define VIAL_UNLOCK_COMBO_COLS {1, 1}
// Fix the native storage layout instead of relying on capacity heuristics.
#define VIAL_TAP_DANCE_ENTRIES 32
#define VIAL_COMBO_ENTRIES 32
#define VIAL_KEY_OVERRIDE_ENTRIES 32
#define VIAL_ALT_REPEAT_KEY_ENTRIES 32
// Reserve a schema marker outside the dynamic storage region.
#define DYNAMIC_KEYMAP_EEPROM_MAX_ADDR 0xFFB
