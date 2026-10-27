#pragma once
struct device {
    int unused;
};
const struct device *device_get_binding(const char *name);
