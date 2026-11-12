/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "controls.h"
#include <errno.h>
#include <drivers/behavior.h>
#include <dt-bindings/zmk/gmk70_controls.h>
#include <zephyr/device.h>
#include <zmk/behavior.h>
#ifdef CONFIG_GMK70_MERGED_CONTROLS
#    include "../function_flip/flip.h"
#    include "../nkro/nkro.h"
#    include "../recovery/recovery.h"
#    include "../wired/wired.h"
#    include <zmk/split/central.h>
#    include <zmk/events/position_state_changed.h>

static int recovery(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event, bool pressed) {
    /* Reboot/bootloader target the key's half. Settings reset stays on LEFT,
     * which owns the joined keyboard's application settings. */
#    if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (binding->param1 != GMK70_RECOVERY_SETTINGS_RESET && gmk_wired_is_left() && event.source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) return zmk_split_central_invoke_behavior(event.source, binding, event, pressed);
#    endif
    return pressed ? gmk_recovery_hold_press((enum gmk_recovery_action)(binding->param1 - GMK70_RECOVERY_RESET + GMK_RECOVERY_RESET), event) : gmk_recovery_release_event(event);
}
#endif

static int controls_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
#ifndef CONFIG_GMK70_MERGED_CONTROLS
    ARG_UNUSED(event);
#endif
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
#ifdef CONFIG_GMK70_MERGED_CONTROLS
        case GMK70_FUNCTION_FLIP:
            return gmk_flip_toggle();
        case GMK70_NKRO_TOGGLE:
            return gmk_nkro_toggle();
        case GMK70_NKRO_ON:
            return gmk_nkro_set(true);
        case GMK70_NKRO_OFF:
            return gmk_nkro_set(false);
        case GMK70_RECOVERY_RESET:
        case GMK70_RECOVERY_BOOTLOADER:
        case GMK70_RECOVERY_SETTINGS_RESET:
            return recovery(binding, event, true);
#endif
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
#ifdef CONFIG_GMK70_MERGED_CONTROLS
static int controls_released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    if (!binding->param2 && binding->param1 >= GMK70_RECOVERY_RESET && binding->param1 <= GMK70_RECOVERY_SETTINGS_RESET) return recovery(binding, event, false);
    return 0;
}
#endif
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
#    ifdef CONFIG_GMK70_MERGED_CONTROLS
#        include "metadata.h"
#        define control_metadata gmk70_menu_metadata[0]
#        define sleep_metadata gmk70_menu_metadata[1]
#    else
#        include "metadata_rows.h"
#        define GMK_VALUE(v, text) {.display_name = (text), .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = (v)},
static const struct behavior_parameter_value_metadata control_values[] = {GMK70_BASE_CONTROL_OPTIONS(GMK_VALUE)};
static const struct behavior_parameter_value_metadata sleep_values[]   = {GMK70_SLEEP_OPTIONS(GMK_VALUE)};
#        undef GMK_VALUE
static const struct behavior_parameter_metadata_set   control_sets[]   = {{.param1_values = control_values, .param1_values_len = ARRAY_SIZE(control_values)}};
static const struct behavior_parameter_metadata_set   sleep_sets[]     = {{.param1_values = sleep_values, .param1_values_len = ARRAY_SIZE(sleep_values)}};
static const struct behavior_parameter_metadata       control_metadata = {.sets_len = ARRAY_SIZE(control_sets), .sets = control_sets};
static const struct behavior_parameter_metadata       sleep_metadata   = {.sets_len = ARRAY_SIZE(sleep_sets), .sets = sleep_sets};
#    endif

#endif
static const struct behavior_driver_api controls_api = {
    .binding_pressed = controls_pressed,
#ifdef CONFIG_GMK70_MERGED_CONTROLS
    .binding_released = controls_released,
#else
    .binding_released = released,
#endif
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
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
