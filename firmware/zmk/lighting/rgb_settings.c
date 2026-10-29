/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <zmk/rgb_underglow.h>

/* Pinned native RGB behavior resolves hue/saturation/brightness commands to
 * set_hsb(), which changes RAM without scheduling its normal settings save.
 * Retain upstream untouched and use the same debounced persistence path. */
int zmk_rgb_underglow_save_state(void);
int __real_zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color);
int __wrap_zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color) {
    int result = __real_zmk_rgb_underglow_set_hsb(color);
    return result ? result : zmk_rgb_underglow_save_state();
}
