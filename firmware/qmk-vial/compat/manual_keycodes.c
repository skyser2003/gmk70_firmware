// SPDX-License-Identifier: GPL-2.0-or-later
#include "stock_keycodes.h"
#include "quantum_keycodes.h"
#include "manual_keycodes.h"

uint16_t gmk70_platform_keycode(uint16_t keycode) {
    // Reuse the original board mode, lock, split-lighting and RGB handlers.
    // Returning a translated lighting code also invokes upstream RGB handling.
    switch (keycode) {
        case GMK70_WINDOWS_MODE:
            return TO(0);
        case GMK70_MAC_MODE:
            return TO(2);
        case GMK70_WINDOWS_LOCK:
            return GU_TOGG;
        case GMK70_RGB_TOGGLE:
            return RM_TOGG;
        case GMK70_RGB_EFFECT_NEXT:
            return RM_NEXT;
        case GMK70_RGB_BRIGHTNESS_UP:
            return RM_VALU;
        case GMK70_RGB_BRIGHTNESS_DN:
            return RM_VALD;
        case GMK70_RGB_SPEED_UP:
            return RM_SPDU;
        case GMK70_RGB_SPEED_DN:
            return RM_SPDD;
        case GMK70_RGB_HUE_UP:
            return RM_HUEU;
        case GMK70_RGB_HUE_DN:
            return RM_HUED;
        case GMK70_RGB_SAT_UP:
            return RM_SATU;
        case GMK70_RGB_SAT_DN:
            return RM_SATD;
        default:
            return keycode;
    }
}
