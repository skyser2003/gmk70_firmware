/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <zmk/rgb_underglow.h>
/* Non-LTO public ABI thunks; rendering/state remain in pinned ZMK. */
extern __typeof__(zmk_rgb_underglow_set_hsb) gmk_native_rgb_underglow_set_hsb;
int                                          zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color) {
    return gmk_native_rgb_underglow_set_hsb(color);
}
extern __typeof__(zmk_rgb_underglow_on) gmk_native_rgb_underglow_on;
int                                     zmk_rgb_underglow_on(void) {
    return gmk_native_rgb_underglow_on();
}
extern __typeof__(zmk_rgb_underglow_off) gmk_native_rgb_underglow_off;
int                                      zmk_rgb_underglow_off(void) {
    return gmk_native_rgb_underglow_off();
}
extern __typeof__(zmk_rgb_underglow_toggle) gmk_native_rgb_underglow_toggle;
int                                         zmk_rgb_underglow_toggle(void) {
    return gmk_native_rgb_underglow_toggle();
}
