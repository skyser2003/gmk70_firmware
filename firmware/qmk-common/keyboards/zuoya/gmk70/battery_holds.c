// SPDX-License-Identifier: GPL-2.0-or-later
#include "battery_holds.h"

bool gmk_battery_holds_set(struct gmk_battery_holds *holds, enum gmk_battery_action action, unsigned position, bool pressed) {
    if ((unsigned)action > GMK_BATTERY_ACTION_CHARGING || position >= GMK_BATTERY_HOLD_POSITIONS) return false;
    uint32_t *word = &holds->positions[action][position / 32];
    uint32_t  mask = 1U << (position % 32);
    if (pressed)
        *word |= mask;
    else
        *word &= ~mask;
    return true;
}

bool gmk_battery_holds_active(const struct gmk_battery_holds *holds, enum gmk_battery_action action) {
    if ((unsigned)action > GMK_BATTERY_ACTION_CHARGING) return false;
    return holds->positions[action][0] || holds->positions[action][1] || holds->positions[action][2];
}

bool gmk_battery_holds_release(struct gmk_battery_holds *holds, unsigned position) {
    if (position >= GMK_BATTERY_HOLD_POSITIONS) return false;
    uint32_t mask    = 1U << (position % 32);
    unsigned word    = position / 32;
    bool     changed = ((holds->positions[GMK_BATTERY_ACTION_LEVEL][word] | holds->positions[GMK_BATTERY_ACTION_CHARGING][word]) & mask) != 0;
    holds->positions[GMK_BATTERY_ACTION_LEVEL][word] &= ~mask;
    holds->positions[GMK_BATTERY_ACTION_CHARGING][word] &= ~mask;
    return changed;
}
