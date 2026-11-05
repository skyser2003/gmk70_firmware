/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native RGB hardware/API boundary; native timer/representation is separately
 * executed by run_rgb_idle_tests.sh. This fixture links the real RGB cache. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <zmk/rgb_underglow.h>
static bool on = true;
int         zmk_rgb_underglow_get_state(bool *value) {
    *value = on;
    return 0;
}
int zmk_rgb_underglow_on(void) {
    on = true;
    return 0;
}
int zmk_rgb_underglow_off(void) {
    on = false;
    return 0;
}
int zmk_rgb_underglow_toggle(void) {
    on = !on;
    return 0;
}
int zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color) {
    (void)color;
    return 0;
}
int zmk_rgb_underglow_save_state(void) {
    return 0;
}
int settings_save_one(const char *name, const void *value, size_t size) {
    (void)name;
    (void)value;
    (void)size;
    return -ENOTSUP;
}
