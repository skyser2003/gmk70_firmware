/* SPDX-License-Identifier: GPL-2.0-or-later */
#define DT_DRV_COMPAT zuoya_gmk70_behavior_radio
#include "radio.h"
#include <errno.h>
#include <drivers/behavior.h>
#include <zephyr/device.h>
#include <zmk/behavior.h>
#include <zmk/events/position_state_changed.h>

static int pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    if (binding->param1 > GMK_HOST_DONGLE || binding->param2) return -EINVAL;
    return gmk_radio_hold((uint8_t)binding->param1, event.position);
}
static int released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    gmk_radio_unhold(event.position);
    return 0;
}
static int position_released(const zmk_event_t *event) {
    /* Studio may replace a held radio binding; its release still cancels hold. */
    const struct zmk_position_state_changed *position = as_zmk_position_state_changed(event);
    if (position && !position->state) gmk_radio_unhold(position->position);
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(gmk_radio_hold_release, position_released);
ZMK_SUBSCRIPTION(gmk_radio_hold_release, zmk_position_state_changed);
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata values[] = {
    {.display_name = "USB", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK_HOST_USB}, {.display_name = "Bluetooth1", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK_HOST_BT1}, {.display_name = "Bluetooth2", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK_HOST_BT2}, {.display_name = "Bluetooth3", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK_HOST_BT3}, {.display_name = "Bluetooth4", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK_HOST_BT4}, {.display_name = "Bluetooth5", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK_HOST_BT5}, {.display_name = "Dongle", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK_HOST_DONGLE},
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
