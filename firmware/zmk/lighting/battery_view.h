/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lighting_frame.h"

struct gmk_battery_status;
enum gmk_battery_indicator { GMK_BATTERY_LEVEL, GMK_BATTERY_CHARGING };
struct gmk_battery_view {
    uint32_t held[2][3];
};

int  gmk_battery_view_set(struct gmk_battery_view *view, enum gmk_battery_indicator kind, uint32_t position, bool pressed);
bool gmk_battery_view_active(const struct gmk_battery_view *view);
bool gmk_battery_view_release(struct gmk_battery_view *view, uint32_t position);
void gmk_battery_view_overlay(const struct gmk_battery_view *view, const struct gmk_battery_status *status, uint8_t pixels[3 * GMK_LIGHT_PIXELS]);
