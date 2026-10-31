/* Native GPIO ABI fixture. The production driver is compiled unchanged. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <zephyr/device.h>
#define BIT(n) (1UL << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
typedef uint32_t gpio_port_pins_t;
typedef uint32_t gpio_port_value_t;
typedef unsigned gpio_pin_t;
typedef unsigned gpio_flags_t;
struct gpio_driver_config {
    gpio_port_pins_t port_pin_mask;
};
struct gpio_driver_data {
    unsigned unused;
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
#define GPIO_INPUT BIT(0)
#define GPIO_OUTPUT BIT(1)
#define GPIO_SINGLE_ENDED BIT(2)
#define GPIO_OUTPUT_INIT_HIGH BIT(3)
#define GPIO_OUTPUT_INIT_LOW BIT(4)
#define GPIO_PULL_UP BIT(5)
#define GPIO_PULL_DOWN BIT(6)
#define GPIO_INT_DISABLE 0
#define GPIO_INT_EDGE_FALLING 1
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
static inline void gpio_init_callback(struct gpio_callback *cb, void (*fn)(const struct device *, struct gpio_callback *, gpio_port_pins_t), gpio_port_pins_t pins) {
    cb->handler  = fn;
    cb->pin_mask = pins;
}
static inline int gpio_add_callback(const struct device *dev, struct gpio_callback *cb) {
    return ((const struct gpio_driver_api *)dev->api)->manage_callback(dev, cb, true);
}
int gpio_pin_interrupt_configure(const struct device *, gpio_pin_t, gpio_flags_t);
