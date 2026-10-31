/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "../../firmware/zmk/nkro/behavior.c"
static unsigned toggles, setter_calls;
static bool     state;
int             gmk_nkro_toggle(void) {
    ++toggles;
    return -EBUSY;
}
int gmk_nkro_set(bool enabled) {
    ++setter_calls;
    state = enabled;
    return -ENODEV;
}
int main(void) {
    struct zmk_behavior_binding       binding = {0};
    struct zmk_behavior_binding_event event   = {.position = 9};
    assert(registered_api->binding_pressed(&binding, event) == -EBUSY && toggles == 1);
    assert(!registered_api->binding_released(&binding, event) && toggles == 1);
    binding.param1 = 1;
    assert(registered_api->binding_pressed(&binding, event) == -ENODEV && setter_calls == 1 && state);
    binding.param1 = 2;
    assert(registered_api->binding_pressed(&binding, event) == -ENODEV && setter_calls == 2 && !state);
    binding.param1 = 3;
    assert(registered_api->binding_pressed(&binding, event) == -EINVAL && setter_calls == 2);
    binding.param1 = 0;
    binding.param2 = 1;
    assert(registered_api->binding_pressed(&binding, event) == -EINVAL && toggles == 1);
    assert(registered_api->locality == BEHAVIOR_LOCALITY_CENTRAL);
#if CONFIG_ZMK_BEHAVIOR_METADATA
    const struct behavior_parameter_metadata *metadata = registered_api->parameter_metadata;
    assert(metadata && metadata->sets_len == 1 && metadata->sets[0].param1_values_len == 3);
    assert(metadata->sets[0].param1_values[0].value == 0);
    assert(metadata->sets[0].param1_values[1].value == 1);
    assert(metadata->sets[0].param1_values[2].value == 2);
#endif
    puts("NKRO behavior: toggle/on/off, validation, release, locality and Studio metadata passed");
}
