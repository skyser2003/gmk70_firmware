// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "action.h"
#include "action_layer.h"
#include "action_util.h"
#include "debug.h"
#include "command.h"
#include "function_flip_qmk.h"
#include "host.h"
#include "keycode_config.h"
#include "nvm_eeconfig.h"
#include "process_keycode/process_magic.h"

static keymap_config_t    saved_config;
static bool               eeprom_enabled;
static unsigned           keymap_writes;
static report_keyboard_t  host_keyboard;
static report_nkro_t      host_nkro;
static unsigned           keyboard_reports;
static unsigned           nkro_reports;
static bool               can_send_nkro = true;
static gmk70_flip_state_t stock_flip;
static uint32_t           stock_board_flags;
static uint16_t           consumer_usage;
keymap_config_t           keymap_config;
debug_config_t            debug_config;
layer_state_t             default_layer_state;
layer_state_t             layer_state;

void nvm_eeconfig_erase(void) {
    saved_config = (keymap_config_t){0};
}
void nvm_eeconfig_enable(void) {
    eeprom_enabled = true;
}
bool nvm_eeconfig_is_enabled(void) {
    return eeprom_enabled;
}
void nvm_eeconfig_update_debug(const debug_config_t *config) {
    (void)config;
}
void nvm_eeconfig_read_debug(debug_config_t *config) {
    *config = (debug_config_t){0};
}
void nvm_eeconfig_update_default_layer(layer_state_t state) {
    (void)state;
}
layer_state_t nvm_eeconfig_read_default_layer(void) {
    return 1;
}
void nvm_eeconfig_read_keymap(keymap_config_t *config) {
    *config = saved_config;
}
void nvm_eeconfig_update_keymap(const keymap_config_t *config) {
    saved_config = *config;
    ++keymap_writes;
}
void nvm_eeconfig_update_kb(uint32_t value) {
    (void)value;
}
void nvm_eeconfig_update_user(uint32_t value) {
    (void)value;
}
void nvm_eeconfig_update_handedness(bool value) {
    (void)value;
}
void default_layer_set(layer_state_t state) {
    default_layer_state = state;
}
layer_state_t layer_state_set_kb(layer_state_t state) {
    return state;
}
void timer_init(void) {}
void sync_timer_init(void) {}
void matrix_init(void) {}
void host_init(void) {}
void led_init_ports(void) {}
void keyboard_post_init_quantum(void) {}
bool host_can_send_nkro(void) {
    return can_send_nkro;
}
void host_keyboard_send(report_keyboard_t *report) {
    host_keyboard = *report;
    ++keyboard_reports;
}
void host_nkro_send(report_nkro_t *report) {
    host_nkro = *report;
    ++nkro_reports;
}
void host_consumer_send(uint16_t usage) {
    consumer_usage = usage;
}
void host_system_send(uint16_t usage) {
    assert(usage == 0);
}
void register_mouse(uint8_t code, bool pressed) {
    (void)code;
    (void)pressed;
    assert(false);
}
void rgb_matrix_decrease_val(void) {}
void rgb_matrix_increase_val(void) {}

// Hardware-only reset dependencies are inert; eeconfig and HID are native.
typedef struct {
    uint8_t cmd;
    uint8_t body[4];
} master_to_slave_t;
typedef struct {
    uint8_t resp;
    uint8_t body[4];
} slave_to_master_t;
enum { USER_SYNC_MMS = 0 };
static bool enable_bat_indicators;
bool        is_keyboard_master(void) {
    return false;
}
bool transaction_rpc_exec(uint8_t id, uint8_t tx_size, const void *tx, uint8_t rx_size, void *rx) {
    (void)id;
    (void)tx_size;
    (void)tx;
    (void)rx_size;
    (void)rx;
    return false;
}
void eeconfig_update_rgb_matrix_default(void) {}
bool hs_reset_settings_user(void) {
    return true;
}
void hs_rgb_blink_set_timer(uint32_t now) {
    (void)now;
}
uint32_t timer_read32(void) {
    return 0;
}
void eeconfig_confinfo_init(void) {}
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} RGB;
void rgb_matrix_hs_indicator_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {
    (void)index;
    (void)rgb;
    (void)interval;
    (void)times;
}

#include "nkro_fixture.h"

static void assert_keyboard_released(void) {
    assert(host_keyboard.mods == 0);
    for (unsigned index = 0; index < sizeof(host_keyboard.keys); ++index)
        assert(host_keyboard.keys[index] == 0);
}

static void assert_nkro_released(void) {
    assert(host_nkro.mods == 0);
    for (unsigned index = 0; index < sizeof(host_nkro.bits); ++index)
        assert(host_nkro.bits[index] == 0);
}

static void press_report(bool nkro) {
    keymap_config.nkro = nkro;
    add_key(KC_A);
    add_key(KC_B);
    set_mods(0x22);
    send_keyboard_report();
    if (nkro) {
        assert(host_nkro.mods == 0x22 && host_nkro.bits[KC_A / 8] == 0x30);
    } else {
        assert(host_keyboard.mods == 0x22 && host_keyboard.keys[0] == KC_A);
    }
}

static void default_on_initializes_empty_eeprom(void) {
    keyboard_init();
    assert(gmk70_stock_nkro_get() == 1);
    assert(saved_config.nkro);
    assert(keymap_writes == 1);
}

static void saved_off_survives_repeated_boot_without_write(void) {
    eeprom_enabled = true;
    saved_config   = (keymap_config_t){.nkro = false, .no_gui = true};
    keyboard_init();
    keyboard_init();
    assert(!keymap_config.nkro && keymap_config.no_gui);
    assert(!saved_config.nkro && keymap_writes == 0);
}

static void live_setter_releases_old_and_inactive_reports_without_saving(void) {
    // Inactive buffers can contain a previous format's keys. A mode round trip
    // must not resurrect them or leave the outgoing host format pressed.
    keyboard_report->keys[0] = KC_Z;
    press_report(true);
    gmk70_stock_nkro_set(0);
    assert(gmk70_stock_nkro_get() == 0 && keymap_writes == 0);
    assert_nkro_released();
    assert_keyboard_released();
    assert(nkro_reports == 2);
    press_report(false);
    gmk70_stock_nkro_set(7);
    assert(gmk70_stock_nkro_get() == 1 && keymap_writes == 0);
    assert_keyboard_released();
    assert_nkro_released();
    assert(keyboard_reports >= 2);
    const unsigned old_reports = nkro_reports;
    gmk70_stock_nkro_set(1);
    assert(nkro_reports == old_reports);
}

static void native_toggle_persists_after_live_override_and_releases_outgoing(void) {
    const uint16_t commands[] = {NK_TOGG, NK_ON, NK_OFF};
    for (unsigned index = 0; index < sizeof(commands) / sizeof(commands[0]); ++index) {
        for (unsigned mode = 0; mode < 2; ++mode) {
            saved_config.nkro = mode == 0;
            press_report(mode != 0);
            keyrecord_t record = {.event = {.pressed = true}};
            assert(board_nkro_record(commands[index], &record));
            assert(!process_magic(commands[index], &record));
            assert_keyboard_released();
            assert_nkro_released();
            assert(keymap_config.nkro == saved_config.nkro);
            const bool expected = commands[index] == NK_ON || (commands[index] == NK_TOGG && mode != 0);
            assert(keymap_config.nkro == expected);
            const unsigned writes = keymap_writes;
            record.event.pressed  = false;
            assert(board_nkro_record(commands[index], &record));
            assert(process_magic(commands[index], &record));
            assert(keymap_writes == writes);
        }
    }
    assert(keymap_writes == 6);
}

static void boot_fallback_mode_changes_discard_inactive_nkro_keys(void) {
    // USB boot protocol forces six-key reports even while NKRO is enabled.
    can_send_nkro               = false;
    keymap_config.nkro          = true;
    nkro_report->bits[KC_Z / 8] = (uint8_t)(1u << (KC_Z % 8));
    add_key(KC_A);
    set_mods(0x02);
    send_keyboard_report();
    assert(host_keyboard.keys[0] == KC_A && keyboard_reports == 1 && nkro_reports == 0);
    gmk70_stock_nkro_set(0);
    assert_keyboard_released();
    assert(nkro_report->bits[KC_Z / 8] == 0);
    assert(keymap_writes == 0);
    can_send_nkro = true;
    gmk70_stock_nkro_set(1);
    send_keyboard_report();
    assert_nkro_released();
}

static void board_reset_restores_native_on_default_and_releases_old_format(void) {
    eeprom_enabled    = true;
    saved_config.nkro = false;
    press_report(false);
    hs_reset_settings();
    assert(keymap_config.nkro && saved_config.nkro);
    assert_keyboard_released();
    assert_nkro_released();
    assert(keymap_writes == 1);
}

static void nkro_controls_release_held_flip_modifiers_and_extra_usages(bool raw_only) {
    const uint16_t held_function = raw_only ? RSFT(KC_F1) : KC_F1;
    for (unsigned control = 0; control < 3; ++control) {
        for (unsigned mode = 0; mode < 2; ++mode) {
            stock_board_flags  = 0x10;
            stock_flip         = (gmk70_flip_state_t){0};
            keymap_config.nkro = mode != 0;
            saved_config.nkro  = mode != 0;
            if (!raw_only) {
                assert(!gmk70_flip_process_record(&stock_flip, KC_3, true, true, true));
                assert(!gmk70_flip_process_record(&stock_flip, RSFT(KC_4), true, true, true));
            }
            assert(!gmk70_flip_process_record(&stock_flip, held_function, true, true, true));
            assert(consumer_usage != 0);
            if (mode)
                assert(host_nkro.mods == (raw_only ? 0x20 : 0x28));
            else
                assert(host_keyboard.mods == (raw_only ? 0x20 : 0x28));

            if (control == 0) {
                gmk70_stock_nkro_set(mode == 0);
            } else if (control == 1) {
                keyrecord_t record = {.event = {.pressed = true}};
                assert(board_nkro_record(NK_TOGG, &record));
                assert(!process_magic(NK_TOGG, &record));
            } else {
                hs_reset_settings();
            }
            // Both actual GNU report-copy wrappers must send the outgoing
            // format without the independent flip modifier contribution.
            assert_keyboard_released();
            assert_nkro_released();
            assert(consumer_usage == 0);
            assert(gmk70_stock_flip_modifiers() == 0);
            if (control != 2) assert(stock_board_flags == 0x10);

            // A later physical release cannot restore old report modifiers;
            // the next press still uses the user's configured flip behavior.
            if (!raw_only) {
                if (gmk70_flip_process_record(&stock_flip, KC_3, false, true, true)) unregister_code(KC_3);
                if (gmk70_flip_process_record(&stock_flip, RSFT(KC_4), false, true, true)) unregister_code(KC_4);
            }
            if (gmk70_flip_process_record(&stock_flip, held_function, false, true, true)) unregister_code(KC_F1);
            assert_keyboard_released();
            assert_nkro_released();
            if (control != 2) {
                assert(!gmk70_flip_process_record(&stock_flip, KC_3, true, (stock_board_flags & 0x10) != 0, true));
                assert(gmk70_stock_flip_modifiers() == 0x08);
                assert(!gmk70_flip_process_record(&stock_flip, KC_3, false, true, true));
                assert_keyboard_released();
                assert_nkro_released();
            }
        }
    }
}

static void both_function_layers_toggle_nkro(void) {
    // Physical V is row 3, column 5 in the authoritative layout.
    assert(keymaps[_FL][3][5] == NK_TOGG);
    assert(keymaps[_MFL][3][5] == NK_TOGG);
    assert(keymaps[_BL][3][5] == KC_V);
    assert(keymaps[_MBL][3][5] == KC_V);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    if (strcmp(argv[1], "default") == 0)
        default_on_initializes_empty_eeprom();
    else if (strcmp(argv[1], "saved") == 0)
        saved_off_survives_repeated_boot_without_write();
    else if (strcmp(argv[1], "live") == 0)
        live_setter_releases_old_and_inactive_reports_without_saving();
    else if (strcmp(argv[1], "toggle") == 0)
        native_toggle_persists_after_live_override_and_releases_outgoing();
    else if (strcmp(argv[1], "boot") == 0)
        boot_fallback_mode_changes_discard_inactive_nkro_keys();
    else if (strcmp(argv[1], "flip") == 0)
        nkro_controls_release_held_flip_modifiers_and_extra_usages(false);
    else if (strcmp(argv[1], "flip-raw") == 0)
        nkro_controls_release_held_flip_modifiers_and_extra_usages(true);
    else if (strcmp(argv[1], "reset") == 0)
        board_reset_restores_native_on_default_and_releases_old_format();
    else if (strcmp(argv[1], "keymaps") == 0)
        both_function_layers_toggle_nkro();
    else
        assert(false);
    return 0;
}
