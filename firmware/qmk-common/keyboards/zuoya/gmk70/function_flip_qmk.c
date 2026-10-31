// SPDX-License-Identifier: GPL-2.0-or-later
#include "function_flip_qmk.h"
#include "quantum.h"
#include "action_util.h"
#include "host.h"
#ifdef NKRO_ENABLE
#    include "keycode_config.h"
#endif

void __real_host_keyboard_send(report_keyboard_t *report);
void __real_host_nkro_send(report_nkro_t *report);

void __wrap_host_keyboard_send(report_keyboard_t *report) {
    report_keyboard_t output = *report;
    output.mods |= gmk70_stock_flip_modifiers();
    __real_host_keyboard_send(&output);
}

void __wrap_host_nkro_send(report_nkro_t *report) {
    report_nkro_t output = *report;
    output.mods |= gmk70_stock_flip_modifiers();
    __real_host_nkro_send(&output);
}

static void sync_modifiers(void) {
    // Refresh native modifiers first. Its native deduplication cache excludes
    // our independent mask, so explicitly send modifier-only transitions too.
    send_keyboard_report();
#ifdef NKRO_ENABLE
    if (host_can_send_nkro() && keymap_config.nkro) {
        host_nkro_send(nkro_report);
        return;
    }
#endif
    host_keyboard_send(keyboard_report);
}

static void apply_transition(gmk70_flip_state_t *state, const gmk70_flip_state_t *next, gmk70_flip_output_t out, bool pressed) {
    if (out.release_previous) unregister_code(out.previous.keycode);
    if (!pressed && !out.passthrough && out.target.keycode) unregister_code(out.target.keycode);

    uint8_t old_modifiers = gmk70_flip_held_modifiers(state);
    *state                = *next;
    if (old_modifiers != gmk70_flip_held_modifiers(state)) sync_modifiers();
    if (pressed && !out.passthrough && out.target.keycode) register_code(out.target.keycode);
    if (out.brightness < 0)
        rgb_matrix_decrease_val();
    else if (out.brightness > 0)
        rgb_matrix_increase_val();
}

bool gmk70_flip_gate_record(gmk70_flip_state_t *state, uint16_t keycode, bool pressed, bool blocked) {
    if (!blocked) return true;
    if (!pressed) {
        gmk70_flip_state_t  next = *state;
        gmk70_flip_output_t out  = gmk70_flip_event(&next, keycode, false, false, false);
        if (out.handled) {
            if (out.passthrough)
                *state = next;
            else
                apply_transition(state, &next, out, false);
        }
    }
    return false;
}

bool gmk70_flip_process_record(gmk70_flip_state_t *state, uint16_t keycode, bool pressed, bool enabled, bool mac) {
    // Retain the old output while releasing a duplicate/replaced target.
    gmk70_flip_state_t  next = *state;
    gmk70_flip_output_t out  = gmk70_flip_event(&next, keycode, pressed, enabled, mac);
    if (!out.handled) return true;
    apply_transition(state, &next, out, pressed);
    return out.passthrough;
}
