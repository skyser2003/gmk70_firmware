// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "function_flip.h"

// Report contribution stays independent of native physical/weak modifiers.
uint8_t gmk70_stock_flip_modifiers(void);
// Preserve admission gates while cleaning up an already-owned rejected release.
bool gmk70_flip_gate_record(gmk70_flip_state_t *state, uint16_t keycode, bool pressed, bool blocked);
bool gmk70_flip_process_record(gmk70_flip_state_t *state, uint16_t keycode, bool pressed, bool enabled, bool mac);
