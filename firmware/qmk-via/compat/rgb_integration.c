// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "rgb_matrix.h"

void __real_rgb_matrix_set_color(int index, uint8_t red, uint8_t green, uint8_t blue);

void __wrap_rgb_matrix_set_color(int index, uint8_t red, uint8_t green, uint8_t blue) {
    // Official QMK already filters LEDs by their owning half and converts the
    // global index to the local driver index. Preserve that implementation.
    __real_rgb_matrix_set_color(index, red, green, blue);
}
