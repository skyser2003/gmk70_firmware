// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "timer.h"
bool is_keyboard_master(void);
bool is_keyboard_left(void);
bool rgb_matrix_is_enabled(void);
bool rgb_matrix_get_suspend_state(void);
void rgb_matrix_set_color(int index, uint8_t red, uint8_t green, uint8_t blue);
void rgb_matrix_set_color_all(uint8_t red, uint8_t green, uint8_t blue);
