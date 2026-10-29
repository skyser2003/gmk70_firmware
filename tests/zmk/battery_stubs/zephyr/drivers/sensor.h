/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#include <zephyr/device.h>
enum sensor_channel {
    SENSOR_CHAN_ALL,
    SENSOR_CHAN_GAUGE_STATE_OF_CHARGE,
    SENSOR_CHAN_GAUGE_VOLTAGE,
};
struct sensor_value {
    int32_t val1, val2;
};
struct sensor_driver_api {
    int (*sample_fetch)(const struct device *, enum sensor_channel);
    int (*channel_get)(const struct device *, enum sensor_channel, struct sensor_value *);
};
