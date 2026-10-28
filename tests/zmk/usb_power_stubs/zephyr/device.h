/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
struct wb32_gpio_config;
struct device {
    const struct wb32_gpio_config *config;
    const void                    *api;
    bool                           ready;
};
#ifdef __cplusplus
extern "C" {
#endif
extern const struct device power_gpio_b;
#ifdef __cplusplus
}
#endif
#define DT_NODELABEL(label) label
#define POWER_DEVICE_GET(node) (&power_gpio_##node)
#define DEVICE_DT_GET(node) POWER_DEVICE_GET(node)
#define power_gpio_gpiob power_gpio_b
#define DEVICE_API(type, name) const struct type##_driver_api name
#define DT_INST_FOREACH_STATUS_OKAY(fn)
static inline bool device_is_ready(const struct device *dev) {
    return dev->ready;
}
