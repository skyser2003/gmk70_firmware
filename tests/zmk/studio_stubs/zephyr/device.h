/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
struct device {
    const char *name;
    bool        ready;
};
static inline bool device_is_ready(const struct device *device) {
    return device && device->ready;
}
