/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
struct zmk_behavior_binding {
    const char *behavior_dev;
    uint32_t    param1, param2;
};
struct zmk_behavior_binding_event {
    int      layer;
    uint32_t position;
    int64_t  timestamp;
};
