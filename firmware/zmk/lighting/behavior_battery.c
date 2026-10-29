/* SPDX-License-Identifier: GPL-2.0-or-later */
#define DT_DRV_COMPAT zuoya_gmk70_behavior_battery
#include "lighting.h"
#include <drivers/behavior.h>
#include <zephyr/device.h>
#include <zmk/behavior.h>
#include <zmk/events/position_state_changed.h>

static int pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    return gmk_lighting_battery_set((enum gmk_battery_indicator)binding->param1, event.position, true);
}
static int released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    return gmk_lighting_battery_release(event.position);
}
/* Studio may replace the held binding with a different behavior entirely.
 * Clear by physical position on release, independent of that new binding. */
static int position_released(const zmk_event_t *event) {
    const struct zmk_position_state_changed *position = as_zmk_position_state_changed(event);
    if (position && !position->state) gmk_lighting_battery_release(position->position);
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(gmk_battery_release, position_released);
ZMK_SUBSCRIPTION(gmk_battery_release, zmk_position_state_changed);
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata values[] = {
    {.display_name = "Hold to show battery level", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK_BATTERY_LEVEL},
    {.display_name = "Hold to show charging status", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK_BATTERY_CHARGING},
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
