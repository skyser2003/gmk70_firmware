/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "../../../wired_stubs/zephyr/drivers/gpio.h"
struct gpio_dt_spec {
    const struct device *port;
    gpio_pin_t           pin;
    gpio_flags_t         dt_flags;
};
int gpio_pin_get_dt(const struct gpio_dt_spec *pin);
int gpio_pin_configure_dt(const struct gpio_dt_spec *pin, gpio_flags_t flags);
