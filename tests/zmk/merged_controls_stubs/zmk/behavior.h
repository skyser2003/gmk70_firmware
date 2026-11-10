/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <zephyr/device.h>
typedef uint16_t zmk_behavior_local_id_t;
struct zmk_behavior_binding {
    uint16_t    local_id;
    const char *behavior_dev;
    uint32_t    param1, param2;
};
struct zmk_behavior_binding_event {
    int      layer;
    uint32_t position;
    int64_t  timestamp;
    uint8_t  source;
};
zmk_behavior_local_id_t zmk_behavior_get_local_id(const char *name);
