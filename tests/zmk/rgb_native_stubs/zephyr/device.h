/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define DT_HAS_CHOSEN(name) 1
#define DT_CHOSEN(name) 0
#define DT_PROP(node, property) 1
struct device {
    int unused;
};
extern const struct device rgb_test_device;
#define DEVICE_DT_GET(node) (&rgb_test_device)
