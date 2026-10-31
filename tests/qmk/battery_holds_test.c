// SPDX-License-Identifier: GPL-2.0-or-later
#include "battery_holds.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    struct gmk_battery_holds holds       = {0};
    const unsigned           positions[] = {0, 31, 32, 63, 64, 71, 89, 90, 95};
    for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
        assert(gmk_battery_holds_set(&holds, GMK_BATTERY_ACTION_LEVEL, positions[i], true));
        assert(gmk_battery_holds_set(&holds, GMK_BATTERY_ACTION_CHARGING, positions[i], true));
    }
    assert(!gmk_battery_holds_set(&holds, GMK_BATTERY_ACTION_LEVEL, 96, true));
    assert(!gmk_battery_holds_set(&holds, (enum gmk_battery_action)2, 0, true));
    for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
        assert(gmk_battery_holds_active(&holds, GMK_BATTERY_ACTION_LEVEL));
        assert(gmk_battery_holds_release(&holds, positions[i]));
        assert(!gmk_battery_holds_release(&holds, positions[i]));
    }
    assert(!gmk_battery_holds_active(&holds, GMK_BATTERY_ACTION_LEVEL));
    assert(!gmk_battery_holds_active(&holds, GMK_BATTERY_ACTION_CHARGING));
    assert(!gmk_battery_holds_release(&holds, 96));
    puts("PASS: shared battery holds, word boundaries, aliases and release-by-position");
}
