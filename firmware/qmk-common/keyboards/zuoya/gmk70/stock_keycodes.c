// SPDX-License-Identifier: GPL-2.0-or-later
#include "stock_keycodes.h"
#include "quantum_keycodes.h"
#include "module.h"
#include "wls/wls.h"

__attribute__((weak)) uint16_t gmk70_platform_keycode(uint16_t keycode) {
    return keycode;
}

uint8_t gmk70_wireless_device(uint16_t keycode) {
    if (keycode >= KC_BT1 && keycode <= KC_BT5) {
        return DEVS_BT1 + (keycode - KC_BT1);
    }
    switch (keycode) {
        case KC_2G4:
            return DEVS_2G4;
        case KC_USB:
            return DEVS_USB;
        default:
            return UINT8_MAX;
    }
}

bool gmk70_device_allowed(uint8_t device, uint8_t physical_mode) {
    if (device > DEVS_2G4) {
        return false;
    }
    if (physical_mode == hs_none || physical_mode == hs_wireless) {
        return true;
    }
    if (device == DEVS_USB) {
        return physical_mode == hs_usb;
    }
    if (device == DEVS_2G4) {
        return physical_mode == hs_2g4;
    }
    return physical_mode == hs_bt;
}

uint16_t gmk70_shortcut_keycode(uint16_t keycode) {
    switch (keycode) {
        case MOR_1:
            return QK_LCTL | KC_Z;
        case MOR_2:
            return QK_LCTL | KC_X;
        case MOR_3:
            return QK_LCTL | KC_C;
        case MOR_4:
            return QK_LCTL | KC_V;
        case MOR_5:
            return QK_LGUI | KC_TAB;
        case MOR_6:
            return QK_LGUI | KC_E;
        default:
            return KC_NO;
    }
}

uint16_t gmk70_rgb_keycode(uint16_t keycode) {
    switch (keycode) {
        case UG_TOGG:
            return RM_TOGG;
        case UG_NEXT:
            return RM_NEXT;
        case UG_PREV:
            return RM_PREV;
        case UG_HUEU:
            return RM_HUEU;
        case UG_HUED:
            return RM_HUED;
        case UG_SATU:
            return RM_SATU;
        case UG_SATD:
            return RM_SATD;
        case UG_VALU:
            return RM_VALU;
        case UG_VALD:
            return RM_VALD;
        case UG_SPDU:
            return RM_SPDU;
        case UG_SPDD:
            return RM_SPDD;
        default:
            return keycode;
    }
}
