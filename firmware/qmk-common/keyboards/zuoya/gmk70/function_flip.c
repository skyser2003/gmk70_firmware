// SPDX-License-Identifier: GPL-2.0-or-later
#include "function_flip.h"
#include "quantum_keycodes.h"

// Native stock KC_FILP mappings. KC_NO marks a keyboard-light action.
static const uint8_t mac_numbers[12]   = {KC_BRID, KC_BRIU, KC_TAB, KC_E, KC_NO, KC_NO, KC_MPRV, KC_MPLY, KC_MNXT, KC_MUTE, KC_VOLD, KC_VOLU};
static const uint8_t mac_functions[12] = {KC_MSEL, KC_VOLD, KC_VOLU, KC_MUTE, KC_MSTP, KC_MPRV, KC_MPLY, KC_MNXT, KC_MAIL, KC_WHOM, KC_CALC, KC_WSCH};

gmk70_flip_output_t gmk70_flip_event(gmk70_flip_state_t *state, uint16_t keycode, bool pressed, bool enabled, bool mac) {
    gmk70_flip_output_t out = {0};
    if (keycode > QK_MODS_MAX) return out;
    uint8_t source = (uint8_t)QK_MODS_GET_BASIC_KEYCODE(keycode);
    uint8_t index;
    if (source >= KC_1 && source <= KC_0)
        index = source - KC_1;
    else if (source == KC_MINS || source == KC_EQL)
        index = (uint8_t)(source - KC_MINS + 10);
    else if (source >= KC_F1 && source <= KC_F12)
        index = (uint8_t)(source - KC_F1 + 12);
    else
        return out;

    uint32_t bit = UINT32_C(1) << index;
    if (!pressed) {
        if (!(state->held_mask & bit)) return out;
        out.handled     = true;
        out.target      = state->held[index];
        out.passthrough = (state->passthrough_mask & bit) != 0;
        state->held_mask &= ~bit;
        state->passthrough_mask &= ~bit;
        return out;
    }

    out.handled     = true;
    out.passthrough = !enabled || (!mac && index >= 12);
    if (state->held_mask & bit) {
        out.previous         = state->held[index];
        out.release_previous = out.previous.keycode != KC_NO;
    }
    uint8_t mods         = QK_MODS_GET_MODS(keycode);
    out.target.keycode   = source;
    out.target.modifiers = (uint8_t)((mods & 0x0f) << ((mods & 0x10) ? 4 : 0));
    if (enabled) {
        if (mac) {
            out.target.keycode = index < 12 ? mac_numbers[index] : mac_functions[index - 12];
            if (index == 2 || index == 3) out.target.modifiers |= 0x08;
            if (index == 4 || index == 5) {
                out.target.modifiers = 0;
                out.brightness       = index == 4 ? -1 : 1;
            }
        } else if (index < 12) {
            out.target.keycode = KC_F1 + index;
        }
    }
    state->held[index] = out.target;
    state->held_mask |= bit;
    if (out.passthrough)
        state->passthrough_mask |= bit;
    else
        state->passthrough_mask &= ~bit;
    return out;
}

uint8_t gmk70_flip_held_modifiers(const gmk70_flip_state_t *state) {
    uint8_t modifiers = 0;
    for (unsigned index = 0; index < 24; ++index) {
        if ((state->held_mask & ~state->passthrough_mask) & (UINT32_C(1) << index)) modifiers |= state->held[index].modifiers;
    }
    return modifiers;
}
