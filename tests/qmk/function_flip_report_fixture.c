// SPDX-License-Identifier: GPL-2.0-or-later
#include <string.h>
#include "function_flip_report_fixture.h"

// Pairing itself is covered by test_pairing.py with native timers/storage.
// This fixture owns only the report boundary used by function/media tests.
void gmk70_pairing_release_position(uint8_t row, uint8_t col) {
    (void)row;
    (void)col;
}
#include "action_util.h"
#include "host.h"
#include "keycode_config.h"

// Preserve the pinned frameworks' native cache semantics in a separate TU,
// where host report calls exercise the production GNU linker wrappers.
static report_keyboard_t native_keyboard, last_keyboard;
static report_nkro_t     native_nkro, last_nkro;
report_keyboard_t       *keyboard_report = &native_keyboard;
report_nkro_t           *nkro_report     = &native_nkro;
keymap_config_t          keymap_config;

bool host_can_send_nkro(void) {
    return true;
}

void function_flip_fixture_reset_reports(void) {
    native_keyboard = (report_keyboard_t){0};
    last_keyboard   = (report_keyboard_t){0};
    native_nkro     = (report_nkro_t){0};
    last_nkro       = (report_nkro_t){0};
}

void send_keyboard_report(void) {
    if (keymap_config.nkro) {
        native_nkro.mods = physical_modifiers | weak_modifiers;
        if (memcmp(&native_nkro, &last_nkro, sizeof(native_nkro)) != 0) {
            last_nkro = native_nkro;
            host_nkro_send(&native_nkro);
        }
    } else {
        native_keyboard.mods = physical_modifiers | weak_modifiers;
        if (memcmp(&native_keyboard, &last_keyboard, sizeof(native_keyboard)) != 0) {
            last_keyboard = native_keyboard;
            host_keyboard_send(&native_keyboard);
        }
    }
}
