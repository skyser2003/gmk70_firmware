/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#include <zephyr/device.h>
struct zmk_behavior_local_id_map {
    const struct device *device;
    uint16_t             local_id;
};
extern struct zmk_behavior_local_id_map settings_test_ids[2];
