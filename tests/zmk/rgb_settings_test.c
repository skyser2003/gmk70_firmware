/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <zmk/rgb_underglow.h>
static int                result, saves;
static struct zmk_led_hsb last;
int                       __real_zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color) {
    last = color;
    return result;
}
int zmk_rgb_underglow_save_state(void) {
    ++saves;
    return 0;
}
int __wrap_zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color);
int main(void) {
    struct zmk_led_hsb color = {.h = 130, .s = 80, .b = 30};
    assert(!__wrap_zmk_rgb_underglow_set_hsb(color));
    assert(saves == 1 && last.h == 130 && last.b == 30);
    result = -22;
    assert(__wrap_zmk_rgb_underglow_set_hsb(color) == -22 && saves == 1);
    puts("RGB settings: successful native HSB changes schedule persistence; errors do not");
}
