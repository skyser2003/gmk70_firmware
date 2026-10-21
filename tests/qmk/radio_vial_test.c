// SPDX-License-Identifier: GPL-2.0-or-later
#define main adapter_fixture_main
#include "vial_adapter_test.c"
#undef main
#include "via.h"
#include "usb_main.h"
#include "startup.h"

void                   __wrap_via_init(void) __attribute__((weak));
void                   __real_via_init(void);
void                   via_init(void);
static unsigned        settings_resets, core_resets, layout_hooks, startup_reads;
uint8_t                debug_config, keymap_config;
layer_state_t          layer_state;
static uint8_t         saved_magic[3] = {0x12, 0x34, 0x56};
static bool            master = true, left = true;
static transport_t     transport = TRANSPORT_USB;
struct test_usb_driver test_usb  = {USB_ACTIVE};
bool                   is_keyboard_master(void) {
    return master;
}
bool is_keyboard_left(void) {
    return left;
}
transport_t get_transport(void) {
    return transport;
}
uint32_t timer_read32(void) {
    return 1234;
}
void    chSysLock(void) {}
void    chSysUnlock(void) {}
uint8_t eeprom_read_byte(const uint8_t *p) {
    assert((uintptr_t)p >= VIA_EEPROM_MAGIC_ADDR && (uintptr_t)p < VIA_EEPROM_MAGIC_ADDR + 3);
    return saved_magic[(uintptr_t)p - VIA_EEPROM_MAGIC_ADDR];
}
void eeprom_update_byte(uint8_t *p, uint8_t value) {
    assert((uintptr_t)p >= VIA_EEPROM_MAGIC_ADDR && (uintptr_t)p < VIA_EEPROM_MAGIC_ADDR + 3);
    saved_magic[(uintptr_t)p - VIA_EEPROM_MAGIC_ADDR] = value;
    ++writes;
}
void nvm_via_read_magic(uint8_t *magic0, uint8_t *magic1, uint8_t *magic2) {
    *magic0 = saved_magic[0];
    *magic1 = saved_magic[1];
    *magic2 = saved_magic[2];
}
void via_init_kb(void) {
    ++layout_hooks;
}
uint32_t via_get_layout_options(void) {
    return 0x90;
}
void via_set_layout_options_kb(uint32_t value) {
    assert(value == 0x90);
    ++layout_hooks;
}
void eeconfig_init_via(void) {
    ++settings_resets;
    via_eeprom_set_valid(true);
}
// Storage boundaries only: eeconfig validity and quantum_init below are the
// actual pinned framework functions, compiled in separate translation units.
bool nvm_eeconfig_is_enabled(void) {
    return __wrap_nvm_eeconfig_is_enabled();
}
bool nvm_eeconfig_is_disabled(void) {
    return !native_enabled;
}
void eeconfig_init(void) {
    ++core_resets;
    __wrap_nvm_eeconfig_enable();
    eeconfig_init_via();
}
void eeconfig_read_debug(uint8_t *value) {
    *value = 0;
    ++startup_reads;
}
void eeconfig_read_keymap(uint8_t *value) {
    *value = 0;
    ++startup_reads;
}
layer_state_t eeconfig_read_default_layer(void) {
    ++startup_reads;
    return 1;
}
void default_layer_set(layer_state_t value) {
    assert(value == 1);
}
void layer_state_set_kb(layer_state_t value) {
    assert(value == layer_state);
}

int main(void) {
    assert(__wrap_via_init != NULL && "Diagnostic build must preserve valid native Vial settings");
    marker         = 0x564D4702;
    native_enabled = true;
    __real_via_init(); // The actual pinned framework resets a different build's magic.
    assert(settings_resets == 1 && writes == 3);
    memcpy(saved_magic, (uint8_t[]){0x12, 0x34, 0x56}, 3);
    settings_resets = writes = layout_hooks = 0;
    // keyboard_init runs via_init before quantum_init. Both validity checks
    // must preserve installed settings; the latter must not clear the maps.
    via_init();
    quantum_init();
    assert(settings_resets == 0 && core_resets == 0 && writes == 0 && layout_hooks == 2);
    assert(startup_reads == 3 && eeconfig_is_enabled() && !eeconfig_is_disabled());
    assert(memcmp(saved_magic, (uint8_t[]){0x12, 0x34, 0x56}, 3) == 0);
    // Explicit invalidation must still work through both framework predicates.
    via_eeprom_set_valid(false);
    assert(!eeconfig_is_enabled() && eeconfig_is_disabled());
    settings_resets = core_resets = writes = 0;
    via_init();
    quantum_init();
    assert(settings_resets == 1 && core_resets == 0 && writes == 3);
    assert(eeconfig_is_enabled() && !eeconfig_is_disabled());

    // Invalid storage schema/core cannot validate even a matching build magic.
    for (unsigned invalid = 0; invalid < 2; ++invalid) {
        marker          = invalid == 0 ? 0 : 0x564D4702;
        native_enabled  = invalid == 0;
        settings_resets = core_resets = writes = 0;
        assert(!eeconfig_is_enabled() && eeconfig_is_disabled());
        via_init();
        quantum_init();
        assert(settings_resets == 1 && core_resets == 1 && writes == 4);
        assert(eeconfig_is_enabled() && !eeconfig_is_disabled());
    }
    const unsigned before_writes = writes;
    for (unsigned gate = 0; gate < 6; ++gate) {
        device                    = gate == 1 ? DEVS_BT2 : DEVS_USB;
        master                    = gate != 2;
        left                      = gate != 3;
        transport                 = gate == 4 ? TRANSPORT_WLS : TRANSPORT_USB;
        test_usb.state            = gate == 5 ? 0 : USB_ACTIVE;
        uint8_t  p[32]            = {0x22, 'R', 'D', 1};
        unsigned before_forwarded = forwarded, before_replies = replies;
        __wrap_raw_hid_receive(p, 32);
        assert(p[5] == (gate == 0 ? 0 : 1));
        assert(forwarded == before_forwarded && replies == before_replies + 1);
        assert(writes == before_writes);
    }
    puts("PASS: real Vial startup preserves valid schema/magic; USB readout gates never mutate EEPROM");
    return 0;
}
