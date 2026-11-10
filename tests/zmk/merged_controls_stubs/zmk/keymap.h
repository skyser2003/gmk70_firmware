/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <zmk/behavior.h>
typedef uint8_t zmk_keymap_layer_id_t;
#define ZMK_KEYMAP_LAYERS_LEN 2
#define ZMK_KEYMAP_LEN 4
const struct zmk_behavior_binding *zmk_keymap_get_layer_binding_at_idx(zmk_keymap_layer_id_t layer, uint16_t index);
int                                zmk_keymap_set_layer_binding_at_idx(zmk_keymap_layer_id_t layer, uint16_t index, struct zmk_behavior_binding binding);
