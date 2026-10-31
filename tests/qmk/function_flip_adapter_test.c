// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "function_flip_qmk.h"
#include "quantum.h"
#include "quantum_keycodes.h"
#include "wireless.h"
#include "function_flip_report_fixture.h"
#include "action_util.h"
#include "host.h"
#include "keycode_config.h"

typedef struct {
    uint8_t keycode;
    uint8_t modifiers;
    bool    pressed;
} host_event_t;

uint8_t                    physical_modifiers;
uint8_t                    weak_modifiers;
static uint8_t             host_modifiers;
static bool                host_keys[256];
static host_event_t        host_events[32];
static unsigned            host_event_count;
static int                 brightness;
static gmk70_flip_state_t *active_state;
static bool                accept_preprocess = true;
static bool                blocked_records;

bool pre_process_record_kb(uint16_t keycode, keyrecord_t *record);

uint8_t gmk70_stock_flip_modifiers(void) {
    return active_state ? gmk70_flip_held_modifiers(active_state) : 0;
}

uint8_t wireless_get_current_devs(void) {
    return DEVS_BT1;
}

void lpwr_activity_trigger(void) {}

void usb_remote_host(void) {}

void stock_power_key(bool pressed) {
    (void)pressed;
}

void stock_battery_release_position(uint8_t position) {
    (void)position;
}

bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
    (void)keycode;
    (void)record;
    return accept_preprocess;
}

void host_keyboard_send(report_keyboard_t *report) {
    host_modifiers = report->mods;
}

void host_nkro_send(report_nkro_t *report) {
    host_modifiers = report->mods;
}

void add_weak_mods(uint8_t modifiers) {
    weak_modifiers |= modifiers;
}

void del_weak_mods(uint8_t modifiers) {
    weak_modifiers &= (uint8_t)~modifiers;
}

void register_code(uint8_t keycode) {
    assert(host_event_count < sizeof(host_events) / sizeof(host_events[0]));
    if (keycode < 0xa5) {
        for (unsigned slot = 0; slot < KEYBOARD_REPORT_KEYS; ++slot) {
            if (!keyboard_report->keys[slot]) {
                keyboard_report->keys[slot] = keycode;
                break;
            }
        }
        nkro_report->bits[keycode / 8] |= (uint8_t)(1u << (keycode % 8));
        send_keyboard_report();
    }
    host_events[host_event_count++] = (host_event_t){.keycode = keycode, .modifiers = host_modifiers, .pressed = true};
    host_keys[keycode]              = true;
}

void unregister_code(uint8_t keycode) {
    assert(host_event_count < sizeof(host_events) / sizeof(host_events[0]));
    if (keycode < 0xa5) {
        for (unsigned slot = 0; slot < KEYBOARD_REPORT_KEYS; ++slot) {
            if (keyboard_report->keys[slot] == keycode) keyboard_report->keys[slot] = 0;
        }
        nkro_report->bits[keycode / 8] &= (uint8_t)~(uint8_t)(1u << (keycode % 8));
        send_keyboard_report();
    }
    host_events[host_event_count++] = (host_event_t){.keycode = keycode, .modifiers = host_modifiers, .pressed = false};
    host_keys[keycode]              = false;
}

void rgb_matrix_decrease_val(void) {
    --brightness;
}

void rgb_matrix_increase_val(void) {
    ++brightness;
}

static void reset_host(void) {
    physical_modifiers = 0;
    weak_modifiers     = 0;
    host_modifiers     = 0;
    host_event_count   = 0;
    brightness         = 0;
    accept_preprocess  = true;
    blocked_records    = false;
    active_state       = NULL;
    function_flip_fixture_reset_reports();
    memset(host_keys, 0, sizeof(host_keys));
}

static bool board_event(gmk70_flip_state_t *state, uint16_t keycode, bool pressed, bool enabled, bool mac) {
    // Both pinned QMK frameworks clear weak modifiers before every keypress.
    if (pressed) weak_modifiers = 0;
    active_state       = state;
    keyrecord_t record = {.event = {.key = {.row = 0, .col = 0}, .type = KEY_EVENT, .pressed = pressed}};
    if (!pre_process_record_kb(keycode, &record)) return false;
    if (!gmk70_flip_gate_record(state, keycode, pressed, blocked_records)) return false;
    return gmk70_flip_process_record(state, keycode, pressed, enabled, mac);
}

static void gated_release_cleans_up_owned_output_before_rejection(void) {
    reset_host();
    gmk70_flip_state_t state = {0};
    assert(!board_event(&state, KC_3, true, true, true));
    assert(keyboard_report->mods == 0 && nkro_report->mods == 0 && weak_modifiers == 0);
    assert(host_keys[0x2b] && host_modifiers == 0x08);
    blocked_records = true; // The board disconnected or USB became suspended.
    assert(!board_event(&state, KC_3, false, false, false));
    assert(!host_keys[0x2b] && host_modifiers == 0 && gmk70_flip_held_modifiers(&state) == 0);
    blocked_records = false;
    assert(board_event(&state, KC_A, true, false, false));
    register_code(KC_A);
    assert(host_modifiers == 0);
}

static void rejected_presses_never_create_owned_outputs(void) {
    reset_host();
    gmk70_flip_state_t state = {0};
    blocked_records          = true;
    assert(!board_event(&state, KC_3, true, true, true));
    assert(host_event_count == 0 && gmk70_flip_held_modifiers(&state) == 0);
    assert(!board_event(&state, KC_3, false, true, true));
    assert(host_event_count == 0);
    blocked_records = false;
    assert(board_event(&state, KC_3, false, true, true));
    assert(host_event_count == 0);
}

static void early_consumed_event_keeps_held_gui_before_late_board_hook(void) {
    reset_host();
    gmk70_flip_state_t state = {0};
    assert(!board_event(&state, KC_3, true, true, true));
    assert(host_keys[0x2b] && host_modifiers == 0x08);
    accept_preprocess = false;
    assert(!board_event(&state, KC_A, true, true, true));
    // A consumer may emit a report without ever reaching process_record_kb.
    send_keyboard_report();
    assert(host_modifiers == 0x08 && host_keys[0x2b]);
    accept_preprocess = true;
    assert(!board_event(&state, KC_3, false, false, false));
    assert(host_modifiers == 0 && !host_keys[0x2b]);
}

static void native_weak_modifier_release_preserves_held_flip_gui(void) {
    reset_host();
    gmk70_flip_state_t state = {0};
    assert(!board_event(&state, KC_3, true, true, true));
    assert(board_event(&state, LGUI(KC_A), true, true, true));
    add_weak_mods(0x08);
    register_code(KC_A);
    assert(board_event(&state, LGUI(KC_A), false, true, true));
    unregister_code(KC_A);
    del_weak_mods(0x08); // Native process_action()/unregister_code16 owns this bit.
    send_keyboard_report();
    assert(host_modifiers == 0x08 && host_keys[0x2b]);
    assert(!board_event(&state, KC_3, false, false, false));
    assert(host_modifiers == 0 && weak_modifiers == 0);
}

static void releasing_flip_preserves_native_weak_modifier_owner(void) {
    reset_host();
    gmk70_flip_state_t state = {0};
    assert(!board_event(&state, KC_3, true, true, true));
    assert(board_event(&state, LGUI(KC_A), true, true, true));
    add_weak_mods(0x08);
    register_code(KC_A);
    assert(!board_event(&state, KC_3, false, false, false));
    assert(host_modifiers == 0x08 && weak_modifiers == 0x08 && host_keys[0x04]);
    assert(board_event(&state, LGUI(KC_A), false, false, false));
    unregister_code(KC_A);
    del_weak_mods(0x08);
    send_keyboard_report();
    assert(host_modifiers == 0);
}

static void mac_gui_remains_held_through_another_press(void) {
    reset_host();
    gmk70_flip_state_t state = {0};
    assert(!board_event(&state, KC_3, true, true, true));
    assert(host_keys[0x2b] && host_modifiers == 0x08 && weak_modifiers == 0);
    assert(board_event(&state, KC_A, true, true, true));
    register_code(KC_A); // Native passthrough after the board hook.
    assert(host_events[1].keycode == 0x04 && host_events[1].modifiers == 0x08);
    assert(!board_event(&state, KC_3, false, false, false));
    assert(!host_keys[0x2b] && host_keys[0x04]);
    assert(host_events[2].keycode == 0x2b && !host_events[2].pressed && host_events[2].modifiers == 0x08);
    assert((physical_modifiers | weak_modifiers) == 0);
}

static void releasing_shortcut_preserves_physical_gui_and_shift(void) {
    reset_host();
    physical_modifiers = 0x28;
    send_keyboard_report();
    gmk70_flip_state_t state = {0};
    assert(!board_event(&state, RSFT(KC_4), true, true, true));
    assert(host_keys[0x08] && host_events[0].modifiers == 0x28);
    assert(!board_event(&state, KC_4, false, false, false));
    assert(!host_keys[0x08] && physical_modifiers == 0x28 && weak_modifiers == 0 && host_modifiers == 0x28);
}

static void releasing_one_shortcut_keeps_another_shortcuts_modifier(void) {
    reset_host();
    gmk70_flip_state_t state = {0};
    assert(!board_event(&state, KC_3, true, true, true));
    assert(!board_event(&state, KC_4, true, true, true));
    assert(!board_event(&state, KC_3, false, true, false));
    assert(!host_keys[0x2b] && host_keys[0x08] && host_modifiers == 0x08 && weak_modifiers == 0);
    assert(!board_event(&state, KC_4, false, false, false));
    assert(!host_keys[0x08] && weak_modifiers == 0);
}

static void duplicate_and_context_changes_release_before_repress(void) {
    reset_host();
    gmk70_flip_state_t state = {0};
    assert(!board_event(&state, KC_3, true, true, true));
    assert(!board_event(&state, KC_3, true, true, false));
    assert(host_event_count == 3);
    assert(host_events[1].keycode == 0x2b && !host_events[1].pressed && host_events[1].modifiers == 0x08);
    assert(host_events[2].keycode == 0x3c && host_events[2].pressed && host_events[2].modifiers == 0);
    assert(!host_keys[0x2b] && host_keys[0x3c] && weak_modifiers == 0);
    assert(!board_event(&state, KC_3, false, false, true));
    assert(!host_keys[0x3c] && weak_modifiers == 0);
}

static void passthrough_keeps_native_processors_across_flip_and_os_changes(void) {
    reset_host();
    gmk70_flip_state_t state = {0};
    assert(board_event(&state, KC_1, true, false, false));
    assert(host_event_count == 0);
    register_code(KC_1); // The native processor owns the unchanged key.
    assert(board_event(&state, KC_1, false, true, true));
    assert(host_event_count == 1);
    unregister_code(KC_1);
    assert(!host_keys[0x1e]);
    assert(board_event(&state, KC_F3, true, true, false));
    assert(board_event(&state, KC_F3, false, true, true));
    assert(host_event_count == 2);
}

static void brightness_stays_local(void) {
    reset_host();
    gmk70_flip_state_t state = {0};
    assert(!board_event(&state, LSFT(KC_5), true, true, true));
    assert(brightness == -1 && host_event_count == 0 && weak_modifiers == 0);
    assert(!board_event(&state, KC_5, false, false, false));
    assert(brightness == -1 && host_event_count == 0);
}

static void consumer_modifier_changes_bypass_unchanged_native_report_cache(void) {
    reset_host();
    gmk70_flip_state_t state = {0};
    assert(!board_event(&state, LCTL(KC_1), true, true, true));
    assert(host_events[0].keycode == 0xbe && host_events[0].modifiers == 0x01);
    assert(host_modifiers == 0x01 && keyboard_report->mods == 0 && nkro_report->mods == 0 && weak_modifiers == 0);
    assert(!board_event(&state, KC_1, false, false, false));
    assert(host_events[1].keycode == 0xbe && !host_events[1].pressed && host_events[1].modifiers == 0x01);
    assert(host_modifiers == 0 && keyboard_report->mods == 0 && nkro_report->mods == 0 && weak_modifiers == 0);
}

int main(void) {
    for (unsigned mode = 0; mode < 2; ++mode) {
        keymap_config.nkro = mode != 0;
        consumer_modifier_changes_bypass_unchanged_native_report_cache();
        gated_release_cleans_up_owned_output_before_rejection();
        rejected_presses_never_create_owned_outputs();
        native_weak_modifier_release_preserves_held_flip_gui();
        releasing_flip_preserves_native_weak_modifier_owner();
        early_consumed_event_keeps_held_gui_before_late_board_hook();
        passthrough_keeps_native_processors_across_flip_and_os_changes();
        mac_gui_remains_held_through_another_press();
        releasing_shortcut_preserves_physical_gui_and_shift();
        releasing_one_shortcut_keeps_another_shortcuts_modifier();
        duplicate_and_context_changes_release_before_repress();
        brightness_stays_local();
    }
    puts("Function flip adapter: held modifiers, physical ownership, duplicates, local brightness and original releases passed");
}
