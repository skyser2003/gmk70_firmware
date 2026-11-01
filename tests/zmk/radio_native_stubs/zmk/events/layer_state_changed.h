/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
static inline int raise_layer_state_changed(uint8_t layer, bool active, bool locked) {
    (void)layer;
    (void)active;
    (void)locked;
    return 0;
}
