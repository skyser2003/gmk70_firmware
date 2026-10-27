#pragma once
#include <stdbool.h>
struct device {
    int unused;
};
extern struct device test_kscan;
bool                 device_is_ready(const struct device *device);
#define DEVICE_DT_GET(node) (&test_kscan)
