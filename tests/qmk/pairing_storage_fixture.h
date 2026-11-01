// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "keycodes.h"
#include "nvm_dynamic_keymap.h"

#ifndef DYNAMIC_KEYMAP_EEPROM_ADDR
#    define DYNAMIC_KEYMAP_EEPROM_ADDR 128
#endif
extern int vial_unlocked;
uint8_t    eeprom_read_byte(const void *address);
void       eeprom_update_byte(void *address, uint8_t value);
uint16_t   keycode_at_keymap_location_raw(uint8_t layer, uint8_t row, uint8_t column);
void       dynamic_keymap_set_keycode(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode);
void       dynamic_keymap_set_buffer(uint16_t offset, uint16_t size, uint8_t *data);
void       dynamic_keymap_reset(void);
