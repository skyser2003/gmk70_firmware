#pragma once
#include <stddef.h>
#include <stdint.h>
struct device;
struct led_rgb {
    uint8_t r, g, b;
};
struct led_strip_driver_api {
    int (*update_rgb)(const struct device *, struct led_rgb *, size_t);
    size_t (*length)(const struct device *);
};
