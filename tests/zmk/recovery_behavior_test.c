/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../firmware/zmk/recovery/recovery.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <drivers/behavior.h>
#include <zmk/events/position_state_changed.h>
#include "../../firmware/zmk/recovery/behavior.c"

static unsigned                 requests;
static enum gmk_recovery_action requested_action;
static int                      request_result;
int                             gmk_recovery_request(enum gmk_recovery_action action) {
    ++requests;
    requested_action = action;
    return request_result;
}

static struct zmk_behavior_binding_event event(uint32_t position, int64_t timestamp) {
    struct zmk_behavior_binding_event result = {.position = position, .timestamp = timestamp};
#if CONFIG_ZMK_SPLIT
    result.source = 0xff;
#endif
    return result;
}

/* A tap or another key's release must not trigger recovery. */
static void test_three_second_hold_and_original_release_ownership(void) {
    struct zmk_behavior_binding binding = {.param1 = GMK_RECOVERY_SETTINGS_RESET};
    assert(registered_api->binding_pressed(&binding, event(9, 100)) == 0);
    assert(registered_api->binding_released(&binding, event(9, 3099)) == 0);
    assert(requests == 0);
    assert(registered_api->binding_pressed(&binding, event(9, 1000)) == 0);
    assert(registered_api->binding_pressed(&binding, event(10, 1100)) == -EBUSY);
    assert(registered_api->binding_released(&binding, event(10, 4100)) == 0);
    assert(requests == 0);
    /* Binding was changed after press; ownership and original action survive. */
    binding.param1 = GMK_RECOVERY_BOOTLOADER;
    assert(registered_api->binding_released(&binding, event(9, 4000)) == 0);
    assert(requests == 1 && requested_action == GMK_RECOVERY_SETTINGS_RESET);
    assert(registered_api->binding_released(&binding, event(9, 5000)) == 0);
    assert(requests == 1);
}

/* A replaced behavior binding still releases through the physical event. */
static void test_physical_release_handles_remapping_and_split_source(void) {
    struct zmk_behavior_binding binding = {.param1 = GMK_RECOVERY_RESET};
    assert(registered_api->binding_pressed(&binding, event(42, 100)) == 0);
    zmk_event_t release = {.position_event = true, .data = {.source = 0xff, .position = 42, .state = false, .timestamp = 3100}};
#if CONFIG_ZMK_SPLIT
    release.data.source = 1;
    assert(registered_listener->callback(&release) == ZMK_EV_EVENT_BUBBLE);
    assert(requests == 1);
    release.data.source = 0xff;
#endif
    assert(registered_listener->callback(&release) == ZMK_EV_EVENT_BUBBLE);
    assert(requests == 2 && requested_action == GMK_RECOVERY_RESET);
    assert(registered_listener->callback(&release) == ZMK_EV_EVENT_BUBBLE);
    assert(requests == 2);
}

static void test_invalid_parameters_and_failed_request(void) {
    struct zmk_behavior_binding binding = {.param1 = 0};
    assert(registered_api->binding_pressed(&binding, event(1, 0)) == -EINVAL);
    binding.param1 = UINT32_MAX;
    assert(registered_api->binding_pressed(&binding, event(1, 0)) == -EINVAL);
    binding.param1 = GMK_RECOVERY_BOOTLOADER;
    binding.param2 = 1;
    assert(registered_api->binding_pressed(&binding, event(1, 0)) == -EINVAL);
    binding.param2 = 0;
    assert(registered_api->binding_pressed(&binding, event(1, 10)) == 0);
    request_result = -EBUSY;
    assert(registered_api->binding_released(&binding, event(1, 3010)) == -EBUSY);
    request_result = 0;
    assert(registered_api->binding_pressed(&binding, event(1, 10)) == 0);
    assert(registered_api->binding_released(&binding, event(1, 3010)) == 0);
    assert(requests == 4);
}

static void test_native_metadata_describes_the_destructive_action(void) {
    assert(registered_api->locality == BEHAVIOR_LOCALITY_EVENT_SOURCE);
#if CONFIG_ZMK_BEHAVIOR_METADATA
    const struct behavior_parameter_metadata *metadata = registered_api->parameter_metadata;
    assert(metadata && metadata->sets_len == 1);
    assert(metadata->sets[0].param1_values_len == 3);
    assert(metadata->sets[0].param1_values[2].value == GMK_RECOVERY_SETTINGS_RESET);
#endif
}

int main(void) {
    test_three_second_hold_and_original_release_ownership();
    test_physical_release_handles_remapping_and_split_source();
    test_invalid_parameters_and_failed_request();
    test_native_metadata_describes_the_destructive_action();
    puts("Recovery behavior: deliberate hold, physical release ownership and native metadata passed");
    return 0;
}
