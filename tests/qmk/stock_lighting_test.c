// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "quantum.h"
#include "rgblight.h"
#include "stock_lighting.h"
#include "rgb_matrix_types.h"
led_config_t   g_led_config;
static uint8_t mode = 14, speed;
static HSV     hsv  = {0, 255, 80};
static RGB     frame[77];
uint8_t        rgblight_get_mode(void) {
    return mode;
}
hsv_t rgblight_get_hsv(void) {
    return hsv;
}
void rgblight_sethsv(uint8_t h, uint8_t s, uint8_t v) {
    hsv = (HSV){h, s, v};
}
void rgblight_mode(uint8_t value) {
    mode = value ? value : 1;
}
void rgblight_set_speed(uint8_t value) {
    speed = value;
}
void rgb_matrix_set_color(int i, uint8_t r, uint8_t g, uint8_t b) {
    assert(i >= 0 && i < 77);
    frame[i] = (RGB){r, g, b};
}
int main(void) {
    memset(&g_led_config, 255, sizeof(g_led_config));
    stock_lighting_init();
    for (unsigned row = 0; row < 10; ++row)
        for (unsigned col = 0; col < 9; ++col)
            assert(g_led_config.matrix_co[row][col] == (row < 5 && col == 8 ? 0 : 255));
    // Captured state sequence from V15 routine 0x080050A0, starting at defaults.
    const uint8_t expected[][5] = {{8, 0, 255, 80, 200}, {2, 0, 255, 80, 200}, {1, 0, 255, 80, 200}, {1, 40, 255, 80, 200}, {1, 80, 255, 80, 200}, {1, 120, 255, 80, 200}, {1, 160, 255, 80, 200}, {1, 200, 255, 80, 200}, {1, 0, 0, 80, 200}, {1, 0, 255, 0, 200}, {14, 0, 255, 80, 255}};
    for (unsigned repetition = 0; repetition < 3; ++repetition)
        for (unsigned i = 0; i < 11; ++i) {
            stock_lighting_step();
            assert(mode == expected[i][0] && hsv.h == expected[i][1] && hsv.s == expected[i][2] && hsv.v == expected[i][3] && speed == expected[i][4]);
        }
    memset(frame, 0x55, sizeof(frame));
    rgblight_driver.set_color_all(1, 2, 3);
    rgblight_driver.set_color(5, 40, 50, 60);
    rgblight_driver.set_color(6, 255, 255, 255); // Out of range cannot corrupt RAM.
    stock_lighting_render();
    for (unsigned i = 0; i < 77; ++i) {
        RGB expected_color = {0x55, 0x55, 0x55};
        if (i == 0 || i == 1 || i == 2 || i == 36 || i == 37) expected_color = (RGB){1, 2, 3};
        if (i == 38) expected_color = (RGB){40, 50, 60};
        assert(memcmp(&frame[i], &expected_color, 3) == 0);
    }
    puts("Stock decorative lighting cycle and six-LED shared-frame routing passed");
}
