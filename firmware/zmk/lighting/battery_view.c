/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "battery_view.h"
#include "../battery/battery.h"
#include <errno.h>
#include <string.h>

static bool held(const struct gmk_battery_view *view, enum gmk_battery_indicator kind) {
    return gmk_battery_holds_active(&view->holds, kind == GMK_BATTERY_LEVEL ? GMK_BATTERY_ACTION_LEVEL : GMK_BATTERY_ACTION_CHARGING);
}

int gmk_battery_view_set(struct gmk_battery_view *view, enum gmk_battery_indicator kind, uint32_t position, bool pressed) {
    if ((unsigned)kind > GMK_BATTERY_CHARGING || position >= 72) return -EINVAL;
    (void)gmk_battery_holds_set(&view->holds, kind == GMK_BATTERY_LEVEL ? GMK_BATTERY_ACTION_LEVEL : GMK_BATTERY_ACTION_CHARGING, position, pressed);
    return 0;
}

bool gmk_battery_view_active(const struct gmk_battery_view *view) {
    return held(view, GMK_BATTERY_LEVEL) || held(view, GMK_BATTERY_CHARGING);
}

bool gmk_battery_view_release(struct gmk_battery_view *view, uint32_t position) {
    if (position >= 72) return false;
    return gmk_battery_holds_release(&view->holds, position);
}

static void color(uint8_t *rgb, unsigned index, uint8_t r, uint8_t g, uint8_t b) {
    rgb[3 * index]     = r;
    rgb[3 * index + 1] = g;
    rgb[3 * index + 2] = b;
}

void gmk_battery_view_overlay(const struct gmk_battery_view *view, const struct gmk_battery_status *status, uint8_t pixels[3 * GMK_LIGHT_PIXELS]) {
    if (held(view, GMK_BATTERY_LEVEL)) {
        static const uint8_t indices[10] = {5, 6, 7, 8, 9, 39, 40, 41, 42, 43};
        memset(pixels, 0, 3 * GMK_LIGHT_PIXELS);
        for (unsigned i = 0; i < 10; ++i) {
            if (!status->valid || status->percentage > 100)
                color(pixels, indices[i], 24, 0, 24);
            else if (i == 0 || i < status->percentage / 10)
                color(pixels, indices[i], status->percentage < 30 ? 80 : 0, status->percentage < 30 ? 0 : 80, 0);
        }
    }
    if (held(view, GMK_BATTERY_CHARGING) && status->cable) color(pixels, 33, status->full ? 0 : 80, status->full ? 80 : 0, 0);
}
