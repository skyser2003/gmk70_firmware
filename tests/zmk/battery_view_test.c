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
static void low_warning_requires_fresh_unpowered_status_and_preserves_overlay_priority(void) {
    struct gmk_battery_status status = {.valid = true, .percentage = 15, .age_ms = 10000};
    assert(gmk_battery_warning_active(&status));
    uint8_t rgb[3 * GMK_LIGHT_PIXELS];
    memset(rgb, 7, sizeof(rgb));
    gmk_battery_warning_overlay(&status, rgb, 249);
    pixel(rgb, 33, 80, 0, 0);
    pixel(rgb, 32, 7, 7, 7);
    pixel(rgb, 34, 7, 7, 7);
    gmk_battery_warning_overlay(&status, rgb, 250);
    pixel(rgb, 33, 0, 0, 0);
    gmk_battery_warning_overlay(&status, rgb, 500);
    pixel(rgb, 33, 80, 0, 0);
    struct gmk_battery_view view = {0};
    assert(!gmk_battery_view_set(&view, GMK_BATTERY_LEVEL, 3, true));
    gmk_battery_view_overlay(&view, &status, rgb);
    pixel(rgb, 33, 0, 0, 0); /* Held level display wins over the automatic warning. */
    for (unsigned invalid = 0; invalid < 5; ++invalid) {
        status = (struct gmk_battery_status){.valid = true, .percentage = 15};
        if (invalid == 0) status.valid = false;
        if (invalid == 1) status.age_ms = 10001;
        if (invalid == 2) status.age_ms = UINT32_MAX;
        if (invalid == 3) status.cable = true;
        if (invalid == 4) status.percentage = 16;
        assert(!gmk_battery_warning_active(&status));
        memset(rgb, 7, sizeof(rgb));
        gmk_battery_warning_overlay(&status, rgb, 0);
        pixel(rgb, 33, 7, 7, 7);
    }
    status = (struct gmk_battery_status){.valid = true, .percentage = 0};
    assert(gmk_battery_warning_active(&status));
    status.cable = true;
    assert(!gmk_battery_view_set(&view, GMK_BATTERY_CHARGING, 5, true));
    gmk_battery_warning_overlay(&status, rgb, 0);
    gmk_battery_view_overlay(&view, &status, rgb);
    pixel(rgb, 33, 80, 0, 0);
    status.full = true;
    gmk_battery_view_overlay(&view, &status, rgb);
    pixel(rgb, 33, 0, 80, 0);
}

int main(void) {
    low_warning_requires_fresh_unpowered_status_and_preserves_overlay_priority();
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
    assert(!gmk_battery_view_release(&view, 74));
    assert(!gmk_battery_view_set(&view, GMK_BATTERY_LEVEL, 73, true));
    assert(gmk_battery_view_release(&view, 73));
    assert(gmk_battery_view_set(&view, 2, 0, true) == -EINVAL);
    assert(gmk_battery_view_set(&view, GMK_BATTERY_LEVEL, 74, true) == -EINVAL);
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
