/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "controls.h"
#include <errno.h>
#include <drivers/behavior.h>
#include <dt-bindings/zmk/gmk70_controls.h>
#include <zephyr/device.h>
#include <zmk/behavior.h>

static int controls_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    if (binding->param2) return -EINVAL;
    switch (binding->param1) {
        case GMK70_GUI_TOGGLE:
            return gmk_controls_gui_lock_toggle();
        case GMK70_GUI_ON:
            return gmk_controls_gui_lock_set(true);
        case GMK70_GUI_OFF:
            return gmk_controls_gui_lock_set(false);
        case GMK70_OS_WINDOWS:
            return gmk_controls_mac_set(false);
        case GMK70_OS_MAC:
            return gmk_controls_mac_set(true);
        case GMK70_OS_TOGGLE:
            return gmk_controls_mac_toggle();
        case GMK70_USB_SLEEP_TOGGLE:
            return gmk_controls_usb_sleep_set(!gmk_controls_usb_sleep());
        case GMK70_USB_SLEEP_ON:
            return gmk_controls_usb_sleep_set(true);
        case GMK70_USB_SLEEP_OFF:
            return gmk_controls_usb_sleep_set(false);
        default:
            return -EINVAL;
    }
}
static int sleep_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    if (binding->param1 > 7 || binding->param2) return -EINVAL;
    return gmk_controls_sleep_selector_set((uint8_t)binding->param1);
}
static int released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return 0;
}
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata control_values[] = {
    {.display_name = "Toggle GUI lock", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK70_GUI_TOGGLE}, {.display_name = "Lock GUI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK70_GUI_ON}, {.display_name = "Unlock GUI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK70_GUI_OFF}, {.display_name = "Windows", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK70_OS_WINDOWS}, {.display_name = "Mac", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK70_OS_MAC}, {.display_name = "Toggle OS", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK70_OS_TOGGLE}, {.display_name = "USB sleep: toggle", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK70_USB_SLEEP_TOGGLE}, {.display_name = "USB sleep: on", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK70_USB_SLEEP_ON}, {.display_name = "USB sleep: off", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK70_USB_SLEEP_OFF},
};
static const struct behavior_parameter_value_metadata sleep_values[] = {
    {.display_name = "Never", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 0}, {.display_name = "1 min", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 1}, {.display_name = "3 min", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 2}, {.display_name = "5 min", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 3}, {.display_name = "10 min", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 4}, {.display_name = "20 min", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 5}, {.display_name = "30 min", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 6}, {.display_name = "Default: 5 min", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 7},
};
static const struct behavior_parameter_metadata_set control_sets[]   = {{.param1_values = control_values, .param1_values_len = ARRAY_SIZE(control_values)}};
static const struct behavior_parameter_metadata_set sleep_sets[]     = {{.param1_values = sleep_values, .param1_values_len = ARRAY_SIZE(sleep_values)}};
static const struct behavior_parameter_metadata     control_metadata = {.sets_len = ARRAY_SIZE(control_sets), .sets = control_sets};
static const struct behavior_parameter_metadata     sleep_metadata   = {.sets_len = ARRAY_SIZE(sleep_sets), .sets = sleep_sets};
#endif
static const struct behavior_driver_api controls_api = {
    .binding_pressed  = controls_pressed,
    .binding_released = released,
    .locality         = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &control_metadata,
#endif
};
static const struct behavior_driver_api sleep_api = {
    .binding_pressed  = sleep_pressed,
    .binding_released = released,
    .locality         = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &sleep_metadata,
#endif
};
BEHAVIOR_DT_DEFINE(DT_INST(0, zuoya_gmk70_behavior_controls), NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &controls_api);
BEHAVIOR_DT_DEFINE(DT_INST(0, zuoya_gmk70_behavior_sleep), NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &sleep_api);
