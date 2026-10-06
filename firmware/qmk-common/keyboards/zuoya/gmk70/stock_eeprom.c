// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "eeprom.h"
#include "rgblight.h"
#include "rgb_matrix_types.h"
#include "version.h"

#ifdef VIA_ENABLE
static bool stock_via_valid(void) {
    return eeprom_read_byte((void *)0x22) == 0x26 && eeprom_read_byte((void *)0x23) == 0x08 && eeprom_read_byte((void *)0x24) == 0x19;
}
#endif

bool __wrap_nvm_eeconfig_is_enabled(void) {
    // V15 0x08007790 validates both the core word and the VIA marker.
    return eeprom_read_word((void *)0) == 0xFEE7
#ifdef VIA_ENABLE
           && stock_via_valid()
#endif
        ;
}

bool __wrap_nvm_eeconfig_is_disabled(void) {
    return eeprom_read_word((void *)0) == 0xFFFF;
}

void __wrap_nvm_eeconfig_enable(void) {
    eeprom_update_word((void *)0, 0xFEE7);
}

void __wrap_nvm_eeconfig_disable(void) {
    // Preserve the upstream format operation, then store stock's disabled word.
    extern void nvm_eeconfig_erase(void);
    nvm_eeconfig_erase();
    eeprom_update_word((void *)0, 0xFFFF);
}

void __wrap_nvm_eeconfig_read_rgb_matrix(rgb_config_t *config) {
    config->raw = 0;
    // V15 0x0800AF08 stores SIX bytes at 0x1C, outside the old LED-matrix slot.
    eeprom_read_block(config, (void *)0x1C, 6);
}

void __wrap_nvm_eeconfig_update_rgb_matrix(const rgb_config_t *config) {
    eeprom_update_block(config, (void *)0x1C, 6);
}

void __wrap_nvm_eeconfig_read_rgblight(rgblight_config_t *config) {
    uint32_t raw   = eeprom_read_dword((void *)8);
    config->raw    = 0;
    config->enable = raw & 1;
    config->mode   = (raw >> 1) & 0x7F;
    config->hue    = raw >> 8;
    config->sat    = raw >> 16;
    config->val    = raw >> 24;
    // V15 persists the four-byte configuration, not the runtime speed byte.
    config->speed = 0;
}

void __wrap_nvm_eeconfig_update_rgblight(const rgblight_config_t *config) {
    uint32_t raw = config->enable | ((uint32_t)config->mode << 1) | ((uint32_t)config->hue << 8) | ((uint32_t)config->sat << 16) | ((uint32_t)config->val << 24);
    eeprom_update_dword((void *)8, raw);
}

#ifdef VIA_ENABLE
void __wrap_nvm_via_read_magic(uint8_t *a, uint8_t *b, uint8_t *c) {
    const char *date  = QMK_BUILDDATE;
    const bool  valid = stock_via_valid();
    if (a) *a = valid ? ((date[2] & 15) << 4) | (date[3] & 15) : 0xFF;
    if (b) *b = valid ? ((date[5] & 15) << 4) | (date[6] & 15) : 0xFF;
    if (c) *c = valid ? ((date[8] & 15) << 4) | (date[9] & 15) : 0xFF;
}

void __wrap_nvm_via_update_magic(uint8_t a, uint8_t b, uint8_t c) {
    bool valid = a != 0xFF || b != 0xFF || c != 0xFF;
    eeprom_update_byte((void *)0x22, valid ? 0x26 : 0xFF);
    eeprom_update_byte((void *)0x23, valid ? 8 : 0xFF);
    eeprom_update_byte((void *)0x24, valid ? 0x19 : 0xFF);
}
#endif
