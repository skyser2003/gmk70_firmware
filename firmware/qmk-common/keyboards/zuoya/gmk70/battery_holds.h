// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum gmk_battery_action { GMK_BATTERY_ACTION_LEVEL, GMK_BATTERY_ACTION_CHARGING };
enum { GMK_BATTERY_HOLD_POSITIONS = 96 };
struct gmk_battery_holds {
    uint32_t positions[2][GMK_BATTERY_HOLD_POSITIONS / 32];
};

bool gmk_battery_holds_set(struct gmk_battery_holds *holds, enum gmk_battery_action action, unsigned position, bool pressed);
bool gmk_battery_holds_active(const struct gmk_battery_holds *holds, enum gmk_battery_action action);
bool gmk_battery_holds_release(struct gmk_battery_holds *holds, unsigned position);
