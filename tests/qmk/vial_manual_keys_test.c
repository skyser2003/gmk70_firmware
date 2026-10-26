// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "quantum_keycodes.h"
#include "stock_keycodes.h"
#include "manual_keycodes.h"

int main(void) {
    // This order is the published User-tab ABI, not the stock reserved gaps.
    const uint16_t controls[] = {GMK70_WINDOWS_MODE, GMK70_MAC_MODE, GMK70_WINDOWS_LOCK, GMK70_RGB_TOGGLE, GMK70_RGB_EFFECT_NEXT, GMK70_RGB_BRIGHTNESS_UP, GMK70_RGB_BRIGHTNESS_DN, GMK70_RGB_SPEED_UP, GMK70_RGB_SPEED_DN, GMK70_RGB_HUE_UP, GMK70_RGB_HUE_DN, GMK70_RGB_SAT_UP, GMK70_RGB_SAT_DN};
    const uint16_t targets[]  = {TO(0), TO(2), GU_TOGG, RM_TOGG, RM_NEXT, RM_VALU, RM_VALD, RM_SPDU, RM_SPDD, RM_HUEU, RM_HUED, RM_SATU, RM_SATD};
    for (unsigned i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
        assert(controls[i] == 0x7E22 + i);
        // Translation terminates at the existing control, and cannot recurse.
        assert(gmk70_platform_keycode(controls[i]) == targets[i]);
        assert(gmk70_platform_keycode(targets[i]) == targets[i]);
        assert(gmk70_rgb_keycode(targets[i]) == targets[i]);
    }
    // The extension must not reinterpret any existing saved keycode, including
    // layers, Vial feature keys, encoder assignments, stock controls and gaps.
    for (uint32_t code = 0; code <= UINT16_MAX; ++code) {
        if (code < 0x7E22 || code > 0x7E2E) assert(gmk70_platform_keycode(code) == code);
    }
    puts("Vial manual controls preserve all other 16-bit keycodes and reuse stock handlers");
}
