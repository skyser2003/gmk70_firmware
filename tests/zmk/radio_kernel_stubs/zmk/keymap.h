/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#include <zmk/behavior.h>
typedef uint8_t zmk_keymap_layer_id_t;
int             zmk_keymap_set_layer_binding_at_idx(zmk_keymap_layer_id_t layer, uint16_t binding_idx, const struct zmk_behavior_binding binding);
