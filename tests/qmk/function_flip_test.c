// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include "function_flip.h"
#include "quantum_keycodes.h"

static void enabled_press_keeps_target_after_disable(void) {
    gmk70_flip_state_t  state = {0};
    gmk70_flip_output_t down  = gmk70_flip_event(&state, KC_1, true, true, false);
    gmk70_flip_output_t up    = gmk70_flip_event(&state, KC_1, false, false, true);
    assert(down.handled && down.target.keycode == 0x3a && down.target.modifiers == 0);
    assert(up.handled && up.target.keycode == 0x3a && up.target.modifiers == 0);
}

static void disabled_press_keeps_original_after_enable(void) {
    gmk70_flip_state_t  state = {0};
    gmk70_flip_output_t down  = gmk70_flip_event(&state, LSFT(KC_3), true, false, false);
    gmk70_flip_output_t up    = gmk70_flip_event(&state, KC_3, false, true, true);
    assert(down.handled && down.target.keycode == 0x20 && down.target.modifiers == 0x02);
    assert(up.handled && up.target.keycode == 0x20 && up.target.modifiers == 0x02);
}

static void mac_shortcut_retains_both_modifier_sides(void) {
    gmk70_flip_state_t  state = {0};
    gmk70_flip_output_t down  = gmk70_flip_event(&state, RSFT(KC_3), true, true, true);
    gmk70_flip_output_t up    = gmk70_flip_event(&state, KC_3, false, true, false);
    assert(down.handled && down.target.keycode == 0x2b && down.target.modifiers == 0x28);
    assert(up.handled && up.target.keycode == 0x2b && up.target.modifiers == 0x28);
}

static void complete_native_stock_tables(void) {
    const uint8_t numbers[]       = {0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2d, 0x2e};
    const uint8_t mac_numbers[]   = {0xbe, 0xbd, 0x2b, 0x08, 0, 0, 0xac, 0xae, 0xab, 0xa8, 0xaa, 0xa9};
    const uint8_t mac_functions[] = {0xaf, 0xaa, 0xa9, 0xa8, 0xad, 0xac, 0xae, 0xab, 0xb1, 0xb5, 0xb2, 0xb4};
    for (unsigned enabled = 0; enabled < 2; ++enabled) {
        for (unsigned mac = 0; mac < 2; ++mac) {
            for (unsigned family = 0; family < 2; ++family) {
                for (unsigned i = 0; i < 12; ++i) {
                    gmk70_flip_state_t  state      = {0};
                    uint8_t             source     = family ? (uint8_t)(0x3a + i) : numbers[i];
                    uint8_t             target     = !enabled ? source : mac ? (family ? mac_functions[i] : mac_numbers[i]) : family ? source : (uint8_t)(0x3a + i);
                    uint8_t             mods       = enabled && mac && !family && (i == 2 || i == 3) ? 0x08 : 0;
                    int8_t              brightness = enabled && mac && !family && i == 4 ? -1 : enabled && mac && !family && i == 5 ? 1 : 0;
                    gmk70_flip_output_t down       = gmk70_flip_event(&state, source, true, enabled, mac);
                    gmk70_flip_output_t up         = gmk70_flip_event(&state, source, false, !enabled, !mac);
                    assert(down.handled && up.handled);
                    assert(down.passthrough == (!enabled || (!mac && family)));
                    assert(up.passthrough == down.passthrough);
                    assert(!down.release_previous && !up.release_previous);
                    assert(down.target.keycode == target && up.target.keycode == target);
                    assert(down.target.modifiers == mods && up.target.modifiers == mods);
                    assert(down.brightness == brightness && up.brightness == 0);
                }
            }
        }
    }
}

static void duplicate_press_releases_old_target_before_replacement(void) {
    gmk70_flip_state_t  state = {0};
    gmk70_flip_output_t out   = gmk70_flip_event(&state, KC_1, true, true, false);
    assert(!out.release_previous && out.target.keycode == 0x3a);
    out = gmk70_flip_event(&state, LCTL(KC_1), true, true, true);
    assert(out.release_previous && out.previous.keycode == 0x3a && out.previous.modifiers == 0);
    assert(out.target.keycode == 0xbe && out.target.modifiers == 0x01);
    out = gmk70_flip_event(&state, KC_1, false, false, false);
    assert(out.handled && out.target.keycode == 0xbe && out.target.modifiers == 0x01);
}

static void brightness_only_slots_never_emit_or_release_host_keys(void) {
    gmk70_flip_state_t  state = {0};
    gmk70_flip_output_t out   = gmk70_flip_event(&state, LSFT(KC_5), true, true, true);
    assert(out.handled && out.target.keycode == 0 && out.target.modifiers == 0 && out.brightness == -1);
    out = gmk70_flip_event(&state, KC_5, true, true, false);
    assert(out.handled && !out.release_previous && out.target.keycode == 0x3e && out.brightness == 0);
    out = gmk70_flip_event(&state, KC_5, true, true, true);
    assert(out.handled && out.release_previous && out.previous.keycode == 0x3e && out.target.keycode == 0);
    out = gmk70_flip_event(&state, KC_5, false, false, false);
    assert(out.handled && out.target.keycode == 0 && out.target.modifiers == 0 && out.brightness == 0);
}

static void unrelated_and_unpaired_releases_pass_through(void) {
    gmk70_flip_state_t state      = {0};
    const uint16_t     ordinary[] = {KC_A, KC_VOLD, KC_LSFT, KC_F24, KC_ENT, KC_ESC, KC_BSPC, KC_TAB, KC_SPC, LT(1, KC_1), MO(1)};
    for (unsigned i = 0; i < sizeof(ordinary) / sizeof(ordinary[0]); ++i) {
        assert(!gmk70_flip_event(&state, ordinary[i], true, true, true).handled);
        assert(!gmk70_flip_event(&state, ordinary[i], false, true, true).handled);
    }
    assert(!gmk70_flip_event(&state, KC_1, false, true, true).handled);
    (void)gmk70_flip_event(&state, KC_1, true, true, true);
    assert(gmk70_flip_event(&state, KC_1, false, true, true).handled);
    assert(!gmk70_flip_event(&state, KC_1, false, true, true).handled);
}

int main(void) {
    enabled_press_keeps_target_after_disable();
    disabled_press_keeps_original_after_enable();
    mac_shortcut_retains_both_modifier_sides();
    complete_native_stock_tables();
    duplicate_press_releases_old_target_before_replacement();
    brightness_only_slots_never_emit_or_release_host_keys();
    unrelated_and_unpaired_releases_pass_through();
    puts("Function flip: native tables, held context/modifiers, duplicates, brightness and passthrough passed");
}
