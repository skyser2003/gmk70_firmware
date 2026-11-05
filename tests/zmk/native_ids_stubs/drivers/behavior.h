/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <zmk/behavior.h>
struct zmk_behavior_ref {
    const struct device *device;
};
struct zmk_behavior_local_id_map {
    const struct device    *device;
    zmk_behavior_local_id_t local_id;
};
extern struct zmk_behavior_ref          native_zmk_behavior_ref[3];
extern struct zmk_behavior_local_id_map native_zmk_behavior_local_id_map[3];
enum behavior_locality { BEHAVIOR_LOCALITY_CENTRAL, BEHAVIOR_LOCALITY_EVENT_SOURCE, BEHAVIOR_LOCALITY_GLOBAL };
const struct device *behavior_get_binding(const char *name);
int                  behavior_keymap_binding_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event);
int                  behavior_keymap_binding_released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event);
int                  behavior_keymap_binding_convert_central_state_dependent_params(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event);
int                  behavior_get_locality(const struct device *device, enum behavior_locality *locality);
