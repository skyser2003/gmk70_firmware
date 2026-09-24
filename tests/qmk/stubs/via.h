// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { id_custom_set_value = 7, id_qmk_rgb_matrix_channel = 3, id_qmk_rgb_matrix_effect = 2 };
bool via_command_kb(uint8_t *data, uint8_t length);
void via_qmk_rgb_matrix_command(uint8_t *data, uint8_t length);
