// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

uint8_t rgb_matrix_is_enabled(void);
bool    rgb_matrix_get_suspend_state(void);
#define RGB_MATRIX_LED_FLUSH_LIMIT 16
void rgb_matrix_set_color(int index, uint8_t red, uint8_t green, uint8_t blue);
void rgb_matrix_set_color_all(uint8_t red, uint8_t green, uint8_t blue);
void rgb_matrix_update_pwm_buffers(void);
