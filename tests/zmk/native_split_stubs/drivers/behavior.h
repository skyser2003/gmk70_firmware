/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <zmk/behavior.h>
int behavior_keymap_binding_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event);
int behavior_keymap_binding_released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event);
