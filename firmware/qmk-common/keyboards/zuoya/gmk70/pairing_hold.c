// SPDX-License-Identifier: GPL-2.0-or-later
#include "pairing_hold.h"
#include "deferred_exec.h"
#include "stock_keycodes.h"
#include "wireless.h"
#include "wls/wls.h"
#ifdef VIA_ENABLE
#    include "nvm_dynamic_keymap.h"
#endif

static struct {
    deferred_token                 token;
    uint8_t                        row;
    uint8_t                        col;
    uint8_t                        device;
    gmk70_pairing_device_allowed_t device_allowed;
} pairing;

static void pairing_cancel(void) {
    if (pairing.token != INVALID_DEFERRED_TOKEN) {
        cancel_deferred_exec(pairing.token);
        pairing.token = INVALID_DEFERRED_TOKEN;
    }
}

void gmk70_pairing_release_position(uint8_t row, uint8_t col) {
    if (row == pairing.row && col == pairing.col) pairing_cancel();
}

static uint32_t pairing_complete(uint32_t trigger_time, void *cb_arg) {
    (void)trigger_time;
    (void)cb_arg;
    pairing.token = INVALID_DEFERRED_TOKEN;
    // A physical selector change during the hold must still forbid pairing.
    if (pairing.device_allowed(pairing.device)) wireless_devs_change(wireless_get_current_devs(), pairing.device, true);
    return 0;
}

bool gmk70_pairing_process_record(uint16_t keycode, keyrecord_t *record, gmk70_pairing_device_allowed_t device_allowed) {
    const uint8_t device   = gmk70_wireless_device(keycode);
    const bool    physical = IS_KEYEVENT(record->event) && record->event.key.row < MATRIX_ROWS && record->event.key.col < MATRIX_COLS;
    if (!record->event.pressed && physical) gmk70_pairing_release_position(record->event.key.row, record->event.key.col);
    if (device == UINT8_MAX) return true;
    if (!record->event.pressed || !device_allowed(device)) return false;

    pairing_cancel();
    if (wireless_get_current_devs() != device) wireless_devs_change(wireless_get_current_devs(), device, false);
    // Synthetic selectors may change hosts, but cannot establish a physical hold.
    if (device != DEVS_USB && physical) {
        pairing.row            = record->event.key.row;
        pairing.col            = record->event.key.col;
        pairing.device         = device;
        pairing.device_allowed = device_allowed;
        pairing.token          = defer_exec(3000, pairing_complete, NULL);
    }
    hs_rgb_blink_set_timer(timer_read32());
    return false;
}

#ifdef VIA_ENABLE
// Both pinned frameworks call these NVM boundaries from dynamic_keymap.c.
// In particular, reset's same-translation-unit setter calls are not wrappable.
// Keep the native implementations responsible for bounds and Vial authorization.
void __real_nvm_dynamic_keymap_update_keycode(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode);
void __real_nvm_dynamic_keymap_update_buffer(uint32_t offset, uint32_t size, uint8_t *data);
void __real_nvm_dynamic_keymap_erase(void);

void __wrap_nvm_dynamic_keymap_update_keycode(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode) {
    pairing_cancel();
    __real_nvm_dynamic_keymap_update_keycode(layer, row, column, keycode);
}

void __wrap_nvm_dynamic_keymap_update_buffer(uint32_t offset, uint32_t size, uint8_t *data) {
    pairing_cancel();
    __real_nvm_dynamic_keymap_update_buffer(offset, size, data);
}

void __wrap_nvm_dynamic_keymap_erase(void) {
    pairing_cancel();
    __real_nvm_dynamic_keymap_erase();
}
#endif
