/* SPDX-License-Identifier: GPL-2.0-or-later */
#define DT_DRV_COMPAT zuoya_gmk70_behavior_nkro
#include "nkro.h"
#include <errno.h>
#include <drivers/behavior.h>
#include <zephyr/device.h>
#include <zmk/behavior.h>

static int pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    if (binding->param2) return -EINVAL;
    switch (binding->param1) {
        case 0:
            return gmk_nkro_toggle();
        case 1:
            return gmk_nkro_set(true);
        case 2:
            return gmk_nkro_set(false);
        default:
            return -EINVAL;
    }
}
static int released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return 0;
}
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata values[] = {
    {.display_name = "Toggle NKRO", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 0},
    {.display_name = "Enable NKRO", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 1},
    {.display_name = "Disable NKRO", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 2},
};
static const struct behavior_parameter_metadata_set sets[]   = {{.param1_values = values, .param1_values_len = ARRAY_SIZE(values)}};
static const struct behavior_parameter_metadata     metadata = {.sets_len = ARRAY_SIZE(sets), .sets = sets};
#endif
static const struct behavior_driver_api api = {
    .binding_pressed  = pressed,
    .binding_released = released,
    .locality         = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};
BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);
