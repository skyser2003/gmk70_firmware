/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <zephyr/device.h>
struct zmk_behavior_binding {
    const char *behavior_dev;
    uint32_t    param1;
    uint32_t    param2;
};
struct zmk_behavior_binding_event {
    int      layer;
    uint32_t position;
    int64_t  timestamp;
#if CONFIG_ZMK_SPLIT
    uint8_t source;
#endif
};
const struct device *zmk_behavior_get_binding(const char *name);
#define ZMK_BEHAVIOR_OPAQUE 0
