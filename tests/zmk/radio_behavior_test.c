/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/zmk/radio/behavior.c"

static unsigned hold_count, unhold_count;
static uint8_t  held_channel;
static uint32_t held_position, released_position;
static int      hold_result;
int             gmk_radio_hold(uint8_t channel, uint32_t position) {
    ++hold_count;
    held_channel  = channel;
    held_position = position;
    return hold_result;
}
void gmk_radio_unhold(uint32_t position) {
    ++unhold_count;
    released_position = position;
}
static void valid_channels_reach_hold_and_preserve_result(void) {
    const struct zmk_behavior_binding_event event = {.layer = 3, .position = 71, .timestamp = 1234};
    assert(registered_api->locality == BEHAVIOR_LOCALITY_CENTRAL);
    for (uint32_t channel = 0; channel <= 6; ++channel) {
        struct zmk_behavior_binding binding = {.behavior_dev = "radio", .param1 = channel};
        hold_result                         = channel == 6 ? -EBUSY : 0;
        assert(registered_api->binding_pressed(&binding, event) == hold_result);
        assert(held_channel == channel && held_position == 71 && hold_count == channel + 1);
    }
}
static void invalid_parameters_do_not_hold_or_wrap_channel(void) {
    const struct zmk_behavior_binding_event event     = {.position = 1};
    const uint32_t                          invalid[] = {7, 256, UINT32_MAX};
    const unsigned                          previous  = hold_count;
    for (unsigned i = 0; i < ARRAY_SIZE(invalid); ++i) {
        struct zmk_behavior_binding binding = {.param1 = invalid[i]};
        assert(registered_api->binding_pressed(&binding, event) == -EINVAL);
    }
    struct zmk_behavior_binding binding = {.param1 = 1, .param2 = 1};
    assert(registered_api->binding_pressed(&binding, event) == -EINVAL);
    assert(hold_count == previous);
}
static void release_cancels_original_position_after_binding_changes(void) {
    struct zmk_behavior_binding             binding = {.behavior_dev = "replacement", .param1 = UINT32_MAX, .param2 = UINT32_MAX};
    const struct zmk_behavior_binding_event event   = {.position = 42};
    assert(registered_api->binding_released(&binding, event) == 0);
    assert(unhold_count == 1 && released_position == 42);
    zmk_event_t position = {.position_event = true, .data = {.source = 0xff, .position = 27, .state = true, .timestamp = 1234}};
    assert(registered_listener->callback(&position) == ZMK_EV_EVENT_BUBBLE);
    assert(unhold_count == 1);
    position.data.state = false;
    assert(registered_listener->callback(&position) == ZMK_EV_EVENT_BUBBLE);
    assert(unhold_count == 2 && released_position == 27);
    position.position_event = false;
    assert(registered_listener->callback(&position) == ZMK_EV_EVENT_BUBBLE);
    assert(unhold_count == 2);
}
#if CONFIG_ZMK_BEHAVIOR_METADATA
static void metadata_exposes_exact_supported_values(void) {
    static const char *const                  names[]    = {"USB", "Bluetooth1", "Bluetooth2", "Bluetooth3", "Bluetooth4", "Bluetooth5", "Dongle"};
    const struct behavior_parameter_metadata *parameters = registered_api->parameter_metadata;
    assert(parameters && parameters->sets_len == 1);
    const struct behavior_parameter_metadata_set *set = parameters->sets;
    assert(set->param1_values_len == 7 && set->param2_values_len == 0 && !set->param2_values);
    for (unsigned i = 0; i < 7; ++i) {
        assert(set->param1_values[i].type == BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE);
        assert(set->param1_values[i].value == i);
        assert(strcmp(set->param1_values[i].display_name, names[i]) == 0);
    }
}
#endif
int main(void) {
    valid_channels_reach_hold_and_preserve_result();
    invalid_parameters_do_not_hold_or_wrap_channel();
    release_cancels_original_position_after_binding_changes();
#if CONFIG_ZMK_BEHAVIOR_METADATA
    metadata_exposes_exact_supported_values();
#endif
    puts("Radio behavior: channel validation, native metadata, central locality and binding-independent release passed");
    return 0;
}
