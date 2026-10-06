// SPDX-License-Identifier: GPL-2.0-or-later
// Use the real pinned QMK configuration types and emulated EEPROM addresses.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "quantum.h"
#include "eeprom.h"
#include "rgblight.h"
#include "rgb_matrix_types.h"
static uint8_t storage[4096];
void           eeprom_read_block(void *dest, const void *src, size_t size) {
    assert((uintptr_t)src + size <= sizeof(storage));
    memcpy(dest, storage + (uintptr_t)src, size);
}
void eeprom_update_block(const void *src, void *dest, size_t size) {
    assert((uintptr_t)dest + size <= sizeof(storage));
    memcpy(storage + (uintptr_t)dest, src, size);
}
uint8_t eeprom_read_byte(const uint8_t *p) {
    uint8_t v;
    eeprom_read_block(&v, p, 1);
    return v;
}
uint16_t eeprom_read_word(const uint16_t *p) {
    uint16_t v;
    eeprom_read_block(&v, p, 2);
    return v;
}
uint32_t eeprom_read_dword(const uint32_t *p) {
    uint32_t v;
    eeprom_read_block(&v, p, 4);
    return v;
}
void eeprom_update_byte(uint8_t *p, uint8_t v) {
    eeprom_update_block(&v, p, 1);
}
void eeprom_update_word(uint16_t *p, uint16_t v) {
    eeprom_update_block(&v, p, 2);
}
void eeprom_update_dword(uint32_t *p, uint32_t v) {
    eeprom_update_block(&v, p, 4);
}
void nvm_eeconfig_erase(void) {
    memset(storage, 0xFF, sizeof(storage));
}
bool __wrap_nvm_eeconfig_is_enabled(void);
bool __wrap_nvm_eeconfig_is_disabled(void);
void __wrap_nvm_eeconfig_enable(void);
void __wrap_nvm_eeconfig_disable(void);
void __wrap_nvm_eeconfig_read_rgb_matrix(rgb_config_t *config);
void __wrap_nvm_eeconfig_update_rgb_matrix(const rgb_config_t *config);
void __wrap_nvm_eeconfig_read_rgblight(rgblight_config_t *config);
void __wrap_nvm_eeconfig_update_rgblight(const rgblight_config_t *config);
void __wrap_nvm_via_read_magic(uint8_t *a, uint8_t *b, uint8_t *c);
void __wrap_nvm_via_update_magic(uint8_t a, uint8_t b, uint8_t c);
int  main(void) {
    nvm_eeconfig_erase();
    __wrap_nvm_eeconfig_enable();
    assert(!__wrap_nvm_eeconfig_is_enabled());
    __wrap_nvm_via_update_magic(0x26, 0x10, 0x05);
    assert(memcmp(storage + 34, (uint8_t[]){0x26, 0x08, 0x19}, 3) == 0);
    assert(__wrap_nvm_eeconfig_is_enabled());
    // Feed the exact stock persisted defaults into the current QMK types.
    memcpy(storage + 8, (uint8_t[]){0x1D, 0, 255, 80}, 4);
    rgblight_config_t light;
    __wrap_nvm_eeconfig_read_rgblight(&light);
    assert(light.enable && light.mode == 14 && !light.velocikey);
    assert(light.hue == 0 && light.sat == 255 && light.val == 80 && light.speed == 0);
    light.speed = 255;
    storage[12] = 0xA5;
    __wrap_nvm_eeconfig_update_rgblight(&light);
    assert(memcmp(storage + 8, (uint8_t[]){0x1D, 0, 255, 80}, 4) == 0 && storage[12] == 0xA5);
    const uint8_t matrix_bytes[] = {0x35, 0, 255, 80, 204, 255};
    memcpy(storage + 28, matrix_bytes, 6);
    rgb_config_t matrix;
    __wrap_nvm_eeconfig_read_rgb_matrix(&matrix);
    assert(matrix.enable == 1 && matrix.mode == 13 && matrix.hsv.v == 80 && matrix.speed == 204 && matrix.flags == 255);
    matrix.hsv.h = 40;
    __wrap_nvm_eeconfig_update_rgb_matrix(&matrix);
    assert(storage[29] == 40 && memcmp(storage + 34, (uint8_t[]){0x26, 0x08, 0x19}, 3) == 0);
    uint8_t a, b, c;
    __wrap_nvm_via_read_magic(&a, &b, &c);
    assert(a == 0x26 && b == 0x10 && c == 0x05); // Translate only the validity API.
    __wrap_nvm_via_update_magic(255, 255, 255);
    assert(!__wrap_nvm_eeconfig_is_enabled());
    __wrap_nvm_via_read_magic(&a, &b, &c);
    assert(a == 255 && b == 255 && c == 255);
    __wrap_nvm_eeconfig_disable();
    assert(__wrap_nvm_eeconfig_is_disabled() && eeprom_read_word(0) == 0xFFFF);
    puts("Stock EEPROM defaults, RGB packing, runtime speed, fixed marker and non-overlapping regions passed");
}
