// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include "stock_keycodes.h"
#include "quantum_keycodes.h"
#include "module.h"
#include "wls/wls.h"

int main(void) {
    // Wire values from the original JSON and stock V13, independent of the enum.
    const uint8_t devices[] = {DEVS_BT1, DEVS_BT2, DEVS_BT3, DEVS_BT4, DEVS_BT5, DEVS_2G4, DEVS_USB};
    for (unsigned slot = 0; slot < sizeof(devices); ++slot) {
        uint8_t device = gmk70_wireless_device(0x7E00 + slot);
        assert(device == devices[slot]);
        for (uint8_t mode = hs_none; mode <= hs_wireless; ++mode) {
            bool expected = mode == hs_none || mode == hs_wireless || (device == DEVS_USB && mode == hs_usb) || (device == DEVS_2G4 && mode == hs_2g4) || (device >= DEVS_BT1 && device <= DEVS_BT5 && mode == hs_bt);
            assert(gmk70_device_allowed(device, mode) == expected);
        }
    }
    assert(KC_BATQ == 0x7E07 && RL_MOD == 0x7E09);
    assert(KC_FILP == 0x7E1A && HS_BATQ == 0x7E1B);
    assert(gmk70_wireless_device(0x7E1B) == UINT8_MAX);
    assert(gmk70_wireless_device(0x0004) == UINT8_MAX);
    assert(!gmk70_device_allowed(UINT8_MAX, hs_none));
    const uint16_t shortcuts[] = {0x011D, 0x011B, 0x0106, 0x0119, 0x082B, 0x0808};
    for (unsigned index = 0; index < sizeof(shortcuts) / sizeof(shortcuts[0]); ++index) {
        assert(gmk70_shortcut_keycode(0x7E1C + index) == shortcuts[index]);
    }
    assert(gmk70_shortcut_keycode(0x7E1B) == 0);
    assert(gmk70_shortcut_keycode(0x7E22) == 0);
    // Saved VIA lighting codes retain matrix behavior after QMK split the APIs.
    const uint16_t legacy_rgb[] = {0x7820, 0x7821, 0x7822, 0x7823, 0x7824, 0x7825, 0x7826, 0x7827, 0x7828, 0x7829, 0x782A};
    const uint16_t matrix_rgb[] = {RM_TOGG, RM_NEXT, RM_PREV, RM_HUEU, RM_HUED, RM_SATU, RM_SATD, RM_VALU, RM_VALD, RM_SPDU, RM_SPDD};
    for (unsigned index = 0; index < sizeof(legacy_rgb) / sizeof(legacy_rgb[0]); ++index) {
        assert(gmk70_rgb_keycode(legacy_rgb[index]) == matrix_rgb[index]);
        assert(gmk70_rgb_keycode(matrix_rgb[index]) == matrix_rgb[index]);
    }
    assert(gmk70_rgb_keycode(KC_A) == KC_A);
    assert(gmk70_rgb_keycode(HS_BATQ) == HS_BATQ);
    puts("Stock keycodes, all device/selector combinations, and shortcuts passed");
}
