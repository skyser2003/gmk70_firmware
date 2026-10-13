// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "rgb_matrix.h"

void __real_rgb_matrix_set_color(int index, uint8_t red, uint8_t green, uint8_t blue);

void __wrap_rgb_matrix_set_color(int index, uint8_t red, uint8_t green, uint8_t blue) {
    if (index < 0 || index >= RGB_MATRIX_LED_COUNT) return;
#ifdef RGB_MATRIX_SPLIT
    static const uint8_t split_count[2] = RGB_MATRIX_SPLIT;
    // This Vial pin converts global right-side indices but does not reject
    // writes belonging to the other half. Board overlays use global indices.
    if ((index < split_count[0]) != is_keyboard_left()) return;
#endif
    // Keep Vial's own global-to-local conversion for LEDs on this half.
    __real_rgb_matrix_set_color(index, red, green, blue);
}
