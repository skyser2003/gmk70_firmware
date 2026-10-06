// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "rgblight.h"
#include "rgblight_drivers.h"
#include "stock_lighting.h"
#include "rgb_matrix_types.h"

extern led_config_t g_led_config;

void stock_lighting_init(void) {
    // V15's lookup table contains zero in the five unwired left-column slots.
    for (uint8_t row = 0; row < 5; ++row)
        g_led_config.matrix_co[row][8] = 0;
}

static RGB decorative[6];

static void decorative_init(void) {}
static void decorative_flush(void) {}
static void decorative_color(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= 0 && index < 6) decorative[index] = (RGB){r, g, b};
}
static void decorative_all(uint8_t r, uint8_t g, uint8_t b) {
    for (uint8_t i = 0; i < 6; ++i)
        decorative_color(i, r, g, b);
}

// The secondary engine shares the 77-LED SPI frame with RGB Matrix. Only the
// matrix driver flushes hardware; RGBLight owns the six decorative positions.
const rgblight_driver_t rgblight_driver = {
    .init          = decorative_init,
    .set_color     = decorative_color,
    .set_color_all = decorative_all,
    .flush         = decorative_flush,
};

void stock_lighting_render(void) {
    static const uint8_t indices[6] = {0, 1, 2, 36, 37, 38};
    for (uint8_t i = 0; i < 6; ++i)
        rgb_matrix_set_color(indices[i], decorative[i].r, decorative[i].g, decorative[i].b);
}

void stock_lighting_step(void) {
    // Reconstructed from V15 0x080050A0, including the zero-value/off cycle.
    static uint8_t       selection;
    static const uint8_t modes[11] = {14, 8, 2, 1, 1, 1, 1, 1, 1, 1, 0};
    uint8_t              mode      = rgblight_get_mode();
    HSV                  hsv       = rgblight_get_hsv();
    if (mode == 1) {
        if (!hsv.v)
            selection = 10;
        else if (!hsv.h)
            selection = hsv.s ? 3 : 9;
        else if (hsv.h == 40)
            selection = 4;
        else if (hsv.h == 80)
            selection = 5;
        else if (hsv.h == 120)
            selection = 6;
        else if (hsv.h == 160)
            selection = 7;
        else if (hsv.h == 200)
            selection = 8;
        else
            selection = 9;
    } else if (mode == 14)
        selection = 0;
    else if (mode == 8)
        selection = 1;
    else if (mode == 2)
        selection = 2;
    else if (!hsv.v)
        selection = 10;

    if (++selection == 11) selection = 0;
    if (selection == 10)
        rgblight_sethsv(0, 255, 0);
    else {
        if (!hsv.v) rgblight_sethsv(0, 255, 80);
        rgblight_mode(modes[selection]);
    }

    hsv = rgblight_get_hsv();
    if (selection >= 3 && selection <= 8)
        rgblight_sethsv((selection - 3) * 40, 255, hsv.v);
    else if (selection == 9)
        rgblight_sethsv(0, 0, hsv.v);
    else
        rgblight_set_speed(selection == 0 ? 255 : 200);
}
