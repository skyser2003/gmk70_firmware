/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "battery_view.h"
#include "battery.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void pixel(const uint8_t *rgb, unsigned i, uint8_t r, uint8_t g, uint8_t b) {
    assert(rgb[3 * i] == r && rgb[3 * i + 1] == g && rgb[3 * i + 2] == b);
}

int main(void) {
    struct gmk_battery_view   view   = {0};
    struct gmk_battery_status status = {.valid = true, .percentage = 50};
    uint8_t                   rgb[3 * GMK_LIGHT_PIXELS];
    memset(rgb, 7, sizeof(rgb));
    gmk_battery_view_overlay(&view, &status, rgb);
    pixel(rgb, 0, 7, 7, 7);
    assert(!gmk_battery_view_active(&view));
    assert(!gmk_battery_view_set(&view, GMK_BATTERY_LEVEL, 12, true));
    assert(!gmk_battery_view_set(&view, GMK_BATTERY_CHARGING, 12, false));
    assert(gmk_battery_view_active(&view));
    assert(gmk_battery_view_release(&view, 12)); /* A live-remapped release clears the original kind. */
    assert(!gmk_battery_view_active(&view));
    assert(!gmk_battery_view_release(&view, 12));
    assert(!gmk_battery_view_release(&view, 72));
    assert(gmk_battery_view_set(&view, 2, 0, true) == -EINVAL);
    assert(gmk_battery_view_set(&view, GMK_BATTERY_LEVEL, 72, true) == -EINVAL);
    assert(!gmk_battery_view_set(&view, GMK_BATTERY_LEVEL, 55, true));
    assert(gmk_battery_view_active(&view));
    gmk_battery_view_overlay(&view, &status, rgb);
    for (unsigned i = 5; i <= 9; ++i)
        pixel(rgb, i, 0, 80, 0);
    for (unsigned i = 39; i <= 43; ++i)
        pixel(rgb, i, 0, 0, 0);
    pixel(rgb, 0, 0, 0, 0);
    status.percentage = 0;
    gmk_battery_view_overlay(&view, &status, rgb);
    pixel(rgb, 5, 80, 0, 0);
    pixel(rgb, 6, 0, 0, 0);
    status.percentage = 100;
    gmk_battery_view_overlay(&view, &status, rgb);
    pixel(rgb, 43, 0, 80, 0);
    status.valid = false;
    gmk_battery_view_overlay(&view, &status, rgb);
    pixel(rgb, 5, 24, 0, 24);
    pixel(rgb, 43, 24, 0, 24);
    assert(!gmk_battery_view_set(&view, GMK_BATTERY_LEVEL, 55, true));
    assert(!gmk_battery_view_set(&view, GMK_BATTERY_LEVEL, 56, true));
    assert(!gmk_battery_view_set(&view, GMK_BATTERY_LEVEL, 55, false));
    assert(gmk_battery_view_active(&view));
    assert(!gmk_battery_view_set(&view, GMK_BATTERY_CHARGING, 3, true));
    status.cable = true;
    gmk_battery_view_overlay(&view, &status, rgb);
    pixel(rgb, 33, 80, 0, 0);
    status.full = true;
    gmk_battery_view_overlay(&view, &status, rgb);
    pixel(rgb, 33, 0, 80, 0);
    assert(!gmk_battery_view_set(&view, GMK_BATTERY_LEVEL, 56, false));
    memset(rgb, 7, sizeof(rgb));
    gmk_battery_view_overlay(&view, &status, rgb);
    pixel(rgb, 0, 7, 7, 7);
    pixel(rgb, 33, 0, 80, 0);
    pixel(rgb, 69, 7, 7, 7); /* Never confuse global LEFT Fn with right-local33. */
    status.cable = false;
    memset(rgb, 7, sizeof(rgb));
    gmk_battery_view_overlay(&view, &status, rgb);
    pixel(rgb, 33, 7, 7, 7);
    assert(!gmk_battery_view_set(&view, GMK_BATTERY_CHARGING, 3, false));
    assert(!gmk_battery_view_active(&view));
    puts("Battery view: held aliases, percentage/unknown, charger input and global LED bounds passed");
}
