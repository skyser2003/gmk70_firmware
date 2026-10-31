// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t keycode;
    uint8_t modifiers;
} gmk70_flip_target_t;

typedef struct {
    gmk70_flip_target_t held[24];
    uint32_t            held_mask;
    // Unchanged presses keep their native QMK processors through release.
    uint32_t passthrough_mask;
} gmk70_flip_state_t;

typedef struct {
    gmk70_flip_target_t target;
    gmk70_flip_target_t previous;
    int8_t              brightness;
    bool                handled;
    bool                passthrough;
    bool                release_previous;
} gmk70_flip_output_t;

// Retain the native QMK target until release, including when flip was disabled.
gmk70_flip_output_t gmk70_flip_event(gmk70_flip_state_t *state, uint16_t keycode, bool pressed, bool enabled, bool mac);
uint8_t             gmk70_flip_held_modifiers(const gmk70_flip_state_t *state);
