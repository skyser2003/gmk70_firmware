/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "../../firmware/zmk/function_flip/behavior.c"
static unsigned toggles;
int             gmk_flip_toggle(void) {
    ++toggles;
    return -EBUSY;
}
int main(void) {
    struct zmk_behavior_binding       binding = {0};
    struct zmk_behavior_binding_event event   = {.position = 9};
    assert(registered_api->binding_pressed(&binding, event) == -EBUSY && toggles == 1);
    assert(registered_api->binding_released(&binding, event) == 0 && toggles == 1);
    binding.param1 = 1;
    assert(registered_api->binding_pressed(&binding, event) == -EINVAL && toggles == 1);
    binding.param1 = 0;
    binding.param2 = 1;
    assert(registered_api->binding_pressed(&binding, event) == -EINVAL && toggles == 1);
    assert(registered_api->locality == BEHAVIOR_LOCALITY_CENTRAL);
#if CONFIG_ZMK_BEHAVIOR_METADATA
    assert(registered_api->parameter_metadata && registered_api->parameter_metadata->sets_len == 0);
#endif
    puts("Function flip behavior: parameterless metadata, validation, press-only toggle and locality passed");
}
