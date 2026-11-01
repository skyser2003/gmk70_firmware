// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "quantum.h"
#include "deferred_exec.h"
#include "stock_keycodes.h"
#include "wls/wls.h"
#include "wireless.h"
#include "pairing_storage_fixture.h"

static struct {
    uint8_t devs;
    uint8_t last_btdevs;
} confinfo              = {.devs = DEVS_BT1, .last_btdevs = DEVS_BT1};
static uint32_t now     = 100;
static uint8_t  current = DEVS_BT1, mode = hs_bt;
static unsigned pairs, switches;
static bool     user_accepts;
static uint8_t  eeprom[1024];
int             vial_unlocked;

uint32_t timer_read32(void) {
    return now;
}
uint8_t wireless_get_current_devs(void) {
    return current;
}
void wireless_devs_change(uint8_t old_device, uint8_t device, bool pairing) {
    assert(old_device == current);
    current = device;
    if (pairing)
        ++pairs;
    else
        ++switches;
}
bool hs_modeio_detection(bool update, uint8_t *detected, uint8_t last) {
    (void)last;
    assert(update);
    *detected = mode;
    return true;
}
void hs_rgb_blink_set_timer(uint32_t value) {
    assert(value == now);
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
    return user_accepts;
}
bool pre_process_record_kb(uint16_t keycode, keyrecord_t *record);

uint8_t eeprom_read_byte(const void *address) {
    assert((uintptr_t)address < sizeof(eeprom));
    return eeprom[(uintptr_t)address];
}
void eeprom_update_byte(void *address, uint8_t value) {
    assert((uintptr_t)address < sizeof(eeprom));
    eeprom[(uintptr_t)address] = value;
}
uint16_t keycode_at_keymap_location_raw(uint8_t layer, uint8_t row, uint8_t column) {
    (void)layer;
    (void)row;
    (void)column;
    return KC_A;
}

#include "pairing_board_fixture.h"

static keyrecord_t physical(uint8_t row, uint8_t col, bool pressed) {
    return (keyrecord_t){.event = {.key = {.row = row, .col = col}, .type = KEY_EVENT, .pressed = pressed}};
}
static void key(uint16_t keycode, uint8_t row, uint8_t col, bool pressed) {
    keyrecord_t record = physical(row, col, pressed);
    (void)pre_process_record_kb(keycode, &record);
    assert(!process_record_wls(keycode, &record));
}
static void advance(uint32_t delay) {
    now += delay;
    deferred_exec_task();
}
static void test_duplicate(void) {
    key(KC_BT1, 0, 1, true);
    advance(1000);
    key(KC_BT1, 7, 2, true);
    key(KC_BT1, 0, 1, false);
    advance(3000);
    assert(pairs == 1 && "Releasing another identically bound position must preserve the newer hold");
}
static void test_remapped_release(void) {
    key(KC_BT1, 7, 2, true);
    keyrecord_t record = physical(7, 2, false);
    // Early physical bookkeeping must run even when a later user hook consumes it.
    assert(!pre_process_record_kb(KC_A, &record));
    assert(process_record_wls(KC_A, &record));
    advance(3000);
    assert(pairs == 0 && "A physical release with changed mapping must cancel pairing");
}
static void test_single_write(void) {
    key(KC_BT1, 0, 1, true);
    dynamic_keymap_set_keycode(0, 0, 1, KC_A);
    advance(3000);
    assert(pairs == 0 && "A live keycode write must proactively cancel pairing");
    assert(eeprom[DYNAMIC_KEYMAP_EEPROM_ADDR + 2] == 0 && eeprom[DYNAMIC_KEYMAP_EEPROM_ADDR + 3] == KC_A);
}
static void test_buffer_write(void) {
    key(KC_BT1, 0, 1, true);
    uint8_t replacement[] = {0, KC_B};
    dynamic_keymap_set_buffer(2, 2, replacement);
    advance(3000);
    assert(pairs == 0 && "A live keymap buffer write must proactively cancel pairing");
    assert(eeprom[DYNAMIC_KEYMAP_EEPROM_ADDR + 2] == 0 && eeprom[DYNAMIC_KEYMAP_EEPROM_ADDR + 3] == KC_B);
}
static void test_reset(void) {
    key(KC_BT1, 0, 1, true);
    dynamic_keymap_reset();
    advance(3000);
    assert(pairs == 0 && "Native reset must proactively cancel pairing");
    assert(eeprom[DYNAMIC_KEYMAP_EEPROM_ADDR + 2] == 0 && eeprom[DYNAMIC_KEYMAP_EEPROM_ADDR + 3] == KC_A && vial_unlocked == 0);
}
static void test_timing(void) {
    key(KC_BT2, 0, 1, true);
    assert(current == DEVS_BT2 && switches == 1);
    advance(2999);
    assert(pairs == 0);
    advance(1);
    assert(pairs == 1);
    advance(3000);
    assert(pairs == 1);
}
static void test_replacement(void) {
    key(KC_BT1, 0, 1, true);
    advance(1000);
    key(KC_BT2, 7, 2, true);
    key(KC_BT1, 0, 1, false);
    advance(2000);
    assert(pairs == 0);
    advance(1000);
    assert(pairs == 1 && current == DEVS_BT2);
}
static void test_mode_gate(void) {
    mode = hs_usb;
    key(KC_BT2, 0, 1, true);
    advance(3000);
    assert(switches == 0 && pairs == 0);
    mode = hs_bt;
    key(KC_BT2, 0, 1, true);
    mode = hs_usb;
    advance(3000);
    assert(pairs == 0 && switches == 1);
}
static void test_usb(void) {
    mode = hs_none;
    key(KC_BT1, 0, 1, true);
    key(KC_USB, 7, 2, true);
    advance(3000);
    assert(pairs == 0 && current == DEVS_USB && switches == 1);
}
static void test_synthetic(void) {
    keyrecord_t record = {.event = {.key = {.row = 0, .col = 1}, .type = COMBO_EVENT, .pressed = true}};
    assert(!process_record_wls(KC_BT1, &record));
    advance(3000);
    assert(pairs == 0 && "Only a physical matrix position may arm a pairing hold");
}
static void test_vial_authorization(void) {
    uint8_t locked[] = {(uint8_t)(QK_BOOT >> 8), (uint8_t)QK_BOOT};
    dynamic_keymap_set_buffer(2, 2, locked);
#ifdef VIAL_ENABLE
    assert(eeprom[DYNAMIC_KEYMAP_EEPROM_ADDR + 2] == 0xff && eeprom[DYNAMIC_KEYMAP_EEPROM_ADDR + 3] == 0xff && vial_unlocked == 0);
#else
    assert(eeprom[DYNAMIC_KEYMAP_EEPROM_ADDR + 2] == (uint8_t)(QK_BOOT >> 8) && eeprom[DYNAMIC_KEYMAP_EEPROM_ADDR + 3] == (uint8_t)QK_BOOT);
#endif
    vial_unlocked      = 1;
    uint8_t unlocked[] = {(uint8_t)(QK_BOOT >> 8), (uint8_t)QK_BOOT};
    dynamic_keymap_set_buffer(2, 2, unlocked);
    assert(eeprom[DYNAMIC_KEYMAP_EEPROM_ADDR + 2] == (uint8_t)(QK_BOOT >> 8) && eeprom[DYNAMIC_KEYMAP_EEPROM_ADDR + 3] == (uint8_t)QK_BOOT && vial_unlocked == 1);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const struct {
        const char *name;
        void (*run)(void);
    } tests[] = {
        {"duplicate", test_duplicate}, {"remapped-release", test_remapped_release}, {"single-write", test_single_write}, {"buffer-write", test_buffer_write}, {"reset", test_reset}, {"timing", test_timing}, {"replacement", test_replacement}, {"mode-gate", test_mode_gate}, {"usb", test_usb}, {"synthetic", test_synthetic}, {"vial-authorization", test_vial_authorization},
    };
    for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (strcmp(argv[1], tests[i].name) == 0) {
            tests[i].run();
            puts(tests[i].name);
            return 0;
        }
    }
    return 1;
}
