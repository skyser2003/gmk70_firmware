// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "eeprom.h"
#include "stock_v15.h"
#include "usb_endpoints.h"
#include "raw_hid.h"
#include "wireless.h"
#include "stock_power.h"
#include "lowpower.h"
#include "rgb_matrix.h"
#include "battery_indicators.h"
#ifdef GMK70_RADIO_DIAGNOSTICS
#    include "via.h"
#    include "nvm_via.h"
#    include "usb_main.h"
#    include "radio_capture.h"

void __real_via_init(void);
void via_init_kb(void);
bool __wrap_nvm_eeconfig_is_enabled(void);

static bool diagnostic_settings_valid(void) {
    // This temporary capture build has the same definition and storage schema.
    // Retain the installed build's magic, so a round trip to it keeps settings.
    // Invalid core/schema or erased magic still follows native initialization.
    if (!__wrap_nvm_eeconfig_is_enabled()) return false;
    uint8_t magic0, magic1, magic2;
    nvm_via_read_magic(&magic0, &magic1, &magic2);
    return magic0 != 0xFF || magic1 != 0xFF || magic2 != 0xFF;
}

bool __wrap_via_eeprom_is_valid(void) {
    // eeconfig_is_enabled() checks this again in quantum_init(), after via_init.
    return diagnostic_settings_valid();
}

void __wrap_via_init(void) {
    // via.c's internal validity call cannot be intercepted by the linker wrap.
    if (diagnostic_settings_valid()) {
        via_init_kb();
        via_set_layout_options_kb(via_get_layout_options());
    } else {
        __real_via_init();
    }
}
#endif
#ifdef GMK70_VIAL_UPDATE
#    include "vial.h"
#    include "update/qmk_update.h"
#    include "update/protocol.h"

bool gmk_update_usb_authorized(uint8_t operation) {
    // Authorize at the USB master. The slave inherits that authorization over
    // the split link; it cannot perform Vial's USB unlock handshake itself.
    return operation == GMK_INFO || operation == GMK_STATUS || vial_unlocked;
}
#endif

#define GMK70_VIAL_SCHEMA_ADDR ((void *)0xFFC)
#define GMK70_VIAL_SCHEMA_MAGIC 0x564D4702u

bool __real_nvm_eeconfig_is_enabled(void);
void __real_nvm_eeconfig_enable(void);
void __real_raw_hid_receive(uint8_t *data, uint8_t length);
void __real_housekeeping_task(void);

void __wrap_housekeeping_task(void) {
    __real_housekeeping_task();
    // Native split RGB sync copies config directly, and VialRGB changes it
    // without the stock C1/C2 toggle RPC. Reconcile each half's LED supply after
    // those updates, including a slave that booted with RGB disabled locally.
    // Stock sleep/wake callbacks retain ownership outside normal operation.
    if (lpwr_get_state() != LPWR_NORMAL) return;
    bool enabled = (rgb_matrix_is_enabled() || gmk70_vial_battery_indicator_requested()) && !rgb_matrix_get_suspend_state();
    gpio_write_pin(LED_POWER_EN_PIN, enabled);
    gpio_write_pin(LED_POWER_EN2_PIN, enabled);
}

bool pre_process_record_kb(uint16_t keycode, keyrecord_t *record) {
    // Run once, before combos, tap dance and repeat keys consume/replay events.
    // Synthetic output must not release the stock physical held-key counter.
    if (IS_EVENT(record->event)) {
        if (wireless_get_current_devs() == DEVS_USB) usb_remote_host();
        if (record->event.pressed) lpwr_activity_trigger();
        if (IS_KEYEVENT(record->event) && record->event.key.row < MATRIX_ROWS && record->event.key.col < MATRIX_COLS) stock_power_key(record->event.pressed);
    }
    return pre_process_record_user(keycode, record);
}

bool __wrap_nvm_eeconfig_is_enabled(void) {
    // Native Vial settings do not share stock V15's packed EEPROM layout.
    return __real_nvm_eeconfig_is_enabled() && eeprom_read_dword(GMK70_VIAL_SCHEMA_ADDR) == GMK70_VIAL_SCHEMA_MAGIC;
}

void __wrap_nvm_eeconfig_enable(void) {
    __real_nvm_eeconfig_enable();
    eeprom_update_dword(GMK70_VIAL_SCHEMA_ADDR, GMK70_VIAL_SCHEMA_MAGIC);
}

void __wrap_raw_hid_receive(uint8_t *data, uint8_t length) {
    if (length != RAW_EPSIZE) return;
    gmk70_stock_activity();
#ifdef GMK70_RADIO_DIAGNOSTICS
    bool allowed = wireless_get_current_devs() == DEVS_USB && get_transport() == TRANSPORT_USB && USB_DRIVER.state == USB_ACTIVE && is_keyboard_left() && is_keyboard_master();
    if (gmk_radio_diag_query(data, length, allowed, timer_read32())) {
        raw_hid_send(data, length);
        return;
    }
#endif
#ifdef GMK70_VIAL_UPDATE
    if (gmk_update_usb(data, length)) {
        raw_hid_send(data, length);
        return;
    }
#endif
    // Retain board controls without replacing Vial's identification, unlock,
    // keymap, macro or bootloader handlers with the stock VIA parser.
    if (data[0] == 0x21) {
        if (data[1])
            gmk70_stock_nkro_set(data[2]);
        else
            data[1] = gmk70_stock_nkro_get();
    } else if ((data[0] == 7 || data[0] == 8) && data[1] == 0 && data[2] == 1) {
        if (data[0] == 7)
            gmk70_stock_sleep_set(data[3]);
        else
            data[3] = gmk70_stock_sleep_get();
    } else {
        __real_raw_hid_receive(data, length);
        return;
    }
    raw_hid_send(data, length);
}
