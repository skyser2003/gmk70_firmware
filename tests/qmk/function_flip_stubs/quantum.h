// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include_next "quantum.h"

void register_code(uint8_t keycode);
void unregister_code(uint8_t keycode);
void add_weak_mods(uint8_t modifiers);
void del_weak_mods(uint8_t modifiers);
void send_keyboard_report(void);
void rgb_matrix_decrease_val(void);
void rgb_matrix_increase_val(void);
