/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
struct led_rgb {
    uint8_t r, g, b;
};
int led_strip_update_rgb(const struct device *device, struct led_rgb *pixels, size_t count);
