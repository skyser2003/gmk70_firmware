#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
typedef void (*kscan_callback_t)(const struct device *, uint32_t, uint32_t, bool);
struct kscan_driver_api {
    int (*config)(const struct device *, kscan_callback_t);
    int (*enable_callback)(const struct device *);
    int (*disable_callback)(const struct device *);
};
