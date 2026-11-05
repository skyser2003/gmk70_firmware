/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/zmk/controls/behavior.c"

static struct {
    bool     gui, mac, usb_sleep;
    uint8_t  selector;
    unsigned writes;
    int      result;
} preferences;

static int set_flag(bool *flag, bool value) {
    ++preferences.writes;
    if (!preferences.result) *flag = value;
    return preferences.result;
}
int gmk_controls_gui_lock_set(bool locked) {
    return set_flag(&preferences.gui, locked);
}
int gmk_controls_gui_lock_toggle(void) {
    return gmk_controls_gui_lock_set(!preferences.gui);
}
int gmk_controls_mac_set(bool mac) {
    return set_flag(&preferences.mac, mac);
}
int gmk_controls_mac_toggle(void) {
    return gmk_controls_mac_set(!preferences.mac);
}
bool gmk_controls_usb_sleep(void) {
    return preferences.usb_sleep;
}
int gmk_controls_usb_sleep_set(bool enabled) {
    return set_flag(&preferences.usb_sleep, enabled);
}
int gmk_controls_sleep_selector_set(uint8_t selector) {
    ++preferences.writes;
    if (!preferences.result) preferences.selector = selector;
    return preferences.result;
}
static int invoke(const struct behavior_driver_api *api, uint32_t value, bool pressed) {
    struct zmk_behavior_binding             binding = {.behavior_dev = "controls", .param1 = value};
    const struct zmk_behavior_binding_event event   = {.position = 41, .timestamp = 1234};
    return pressed ? api->binding_pressed(&binding, event) : api->binding_released(&binding, event);
}
static void explicit_actions_update_only_the_requested_preference_on_press(void) {
    struct {
        uint32_t action;
        bool     gui, mac, usb_sleep;
    } const transitions[] = {
        {GMK70_GUI_TOGGLE, true, false, false}, {GMK70_GUI_ON, true, false, false}, {GMK70_GUI_OFF, false, false, false}, {GMK70_OS_MAC, false, true, false}, {GMK70_OS_WINDOWS, false, false, false}, {GMK70_OS_TOGGLE, false, true, false}, {GMK70_USB_SLEEP_TOGGLE, false, true, true}, {GMK70_USB_SLEEP_ON, false, true, true}, {GMK70_USB_SLEEP_OFF, false, true, false},
    };
    for (unsigned index = 0; index < ARRAY_SIZE(transitions); ++index) {
        assert(invoke(&controls_api, transitions[index].action, true) == 0);
        assert(preferences.gui == transitions[index].gui && preferences.mac == transitions[index].mac && preferences.usb_sleep == transitions[index].usb_sleep);
        unsigned writes = preferences.writes;
        assert(invoke(&controls_api, transitions[index].action, false) == 0);
        assert(preferences.writes == writes);
    }
}
static void sleep_selector_preserves_all_eight_stock_values(void) {
    for (uint32_t selector = 0; selector <= 7; ++selector) {
        assert(invoke(&sleep_api, selector, true) == 0);
        assert(preferences.selector == selector);
        unsigned writes = preferences.writes;
        assert(invoke(&sleep_api, selector, false) == 0 && preferences.writes == writes);
    }
}
static void invalid_bindings_cannot_wrap_or_change_preferences(void) {
    const struct behavior_driver_api *apis[]    = {&controls_api, &sleep_api};
    const uint32_t                    invalid[] = {256, UINT32_MAX};
    unsigned                          writes    = preferences.writes;
    for (unsigned index = 0; index < ARRAY_SIZE(apis); ++index) {
        for (unsigned sample = 0; sample < ARRAY_SIZE(invalid); ++sample)
            assert(invoke(apis[index], invalid[sample], true) == -EINVAL);
        struct zmk_behavior_binding             binding = {.param1 = 0, .param2 = 1};
        const struct zmk_behavior_binding_event event   = {0};
        assert(apis[index]->binding_pressed(&binding, event) == -EINVAL);
        binding.param1 = binding.param2 = UINT32_MAX;
        assert(apis[index]->binding_released(&binding, event) == 0);
    }
    assert(invoke(&controls_api, 9, true) == -EINVAL);
    assert(invoke(&sleep_api, 8, true) == -EINVAL);
    assert(preferences.writes == writes);
}
static void setter_failures_are_returned_without_changing_preference_state(void) {
    preferences.result = -EBUSY;
    bool    gui = preferences.gui, mac = preferences.mac, usb_sleep = preferences.usb_sleep;
    uint8_t selector = preferences.selector;
    for (uint32_t action = 0; action <= 8; ++action)
        assert(invoke(&controls_api, action, true) == -EBUSY);
    assert(invoke(&sleep_api, 0, true) == -EBUSY);
    assert(preferences.gui == gui && preferences.mac == mac && preferences.usb_sleep == usb_sleep && preferences.selector == selector);
}
static void metadata_uses_native_one_parameter_enumerations_and_central_locality(void) {
    assert(controls_api.locality == BEHAVIOR_LOCALITY_CENTRAL && sleep_api.locality == BEHAVIOR_LOCALITY_CENTRAL);
#if CONFIG_ZMK_BEHAVIOR_METADATA
    const struct behavior_driver_api *apis[]    = {&controls_api, &sleep_api};
    const size_t                      lengths[] = {9, 8};
    for (unsigned index = 0; index < ARRAY_SIZE(apis); ++index) {
        const struct behavior_parameter_metadata *metadata = apis[index]->parameter_metadata;
        assert(metadata && metadata->sets_len == 1);
        const struct behavior_parameter_metadata_set *set = metadata->sets;
        assert(set->param1_values_len == lengths[index]);
        assert(!set->param2_values && set->param2_values_len == 0);
        for (unsigned value = 0; value < lengths[index]; ++value) {
            assert(set->param1_values[value].type == BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE);
            assert(set->param1_values[value].value == value);
            assert(set->param1_values[value].display_name && strlen(set->param1_values[value].display_name));
        }
    }
#endif
}
int main(void) {
    explicit_actions_update_only_the_requested_preference_on_press();
    sleep_selector_preserves_all_eight_stock_values();
    invalid_bindings_cannot_wrap_or_change_preferences();
    setter_failures_are_returned_without_changing_preference_state();
    metadata_uses_native_one_parameter_enumerations_and_central_locality();
    puts("Controls behaviors: explicit actions, sleep selectors, native metadata and central press-only dispatch passed");
    return 0;
}
