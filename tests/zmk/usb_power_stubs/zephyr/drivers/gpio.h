/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/dt-bindings/gpio/gpio.h>
#define BIT(n) (1U << (n))
#define ARG_UNUSED(x) (void)(x)
#define GPIO_INPUT BIT(16)
#define GPIO_OUTPUT BIT(17)
#define GPIO_OUTPUT_INIT_LOW BIT(18)
#define GPIO_OUTPUT_INIT_HIGH BIT(19)
#define GPIO_OUTPUT_LOW (GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW)
typedef unsigned gpio_pin_t;
typedef uint32_t gpio_flags_t;
typedef uint32_t gpio_port_value_t;
typedef uint32_t gpio_port_pins_t;
struct gpio_driver_config {
    uint32_t port_pin_mask;
};
struct gpio_driver_data {
    uint32_t unused;
};
struct gpio_callback;
typedef struct {
    struct gpio_callback *head;
} sys_slist_t;
struct gpio_callback {
    struct gpio_callback *next;
    void (*handler)(const struct device *, struct gpio_callback *, gpio_port_pins_t);
    gpio_port_pins_t pin_mask;
};
enum gpio_int_mode { GPIO_INT_MODE_DISABLED, GPIO_INT_MODE_LEVEL, GPIO_INT_MODE_EDGE };
enum gpio_int_trig { GPIO_INT_TRIG_LOW = 1, GPIO_INT_TRIG_HIGH = 2, GPIO_INT_TRIG_BOTH = 3 };
struct gpio_driver_api {
    int (*pin_configure)(const struct device *, gpio_pin_t, gpio_flags_t);
    int (*port_get_raw)(const struct device *, gpio_port_value_t *);
    int (*port_set_masked_raw)(const struct device *, gpio_port_pins_t, gpio_port_value_t);
    int (*port_set_bits_raw)(const struct device *, gpio_port_pins_t);
    int (*port_clear_bits_raw)(const struct device *, gpio_port_pins_t);
    int (*port_toggle_bits)(const struct device *, gpio_port_pins_t);
    int (*pin_interrupt_configure)(const struct device *, gpio_pin_t, enum gpio_int_mode, enum gpio_int_trig);
    int (*manage_callback)(const struct device *, struct gpio_callback *, bool);
};
/* Native API dispatch boundary; the callback is the actual WB32 driver. */
static inline int gpio_pin_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags) {
    return ((const struct gpio_driver_api *)dev->api)->pin_configure(dev, pin, flags);
}
