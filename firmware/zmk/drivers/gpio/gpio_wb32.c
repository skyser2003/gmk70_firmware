/* SPDX-License-Identifier: Apache-2.0 */
#define DT_DRV_COMPAT westberry_wb32_gpio
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <soc.h>

struct wb32_gpio_config {
    struct gpio_driver_config common;
    struct wb32_gpio         *port;
    uint8_t                   clock_bit;
};
struct wb32_gpio_data {
    struct gpio_driver_data common;
};

static int wb32_pin_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags) {
    const struct wb32_gpio_config *config = dev->config;
    struct wb32_gpio              *port   = config->port;
    uint32_t                       mode;

    if (pin >= 16 || ((flags & GPIO_INPUT) && (flags & GPIO_OUTPUT)) || (flags & GPIO_SINGLE_ENDED)) {
        return -ENOTSUP;
    }
    mode             = flags & GPIO_OUTPUT ? 1U : flags & GPIO_INPUT ? 0U : 3U;
    unsigned int key = irq_lock();
    port->cfgmsk     = (uint16_t)~BIT(pin);
    if (flags & GPIO_OUTPUT_INIT_HIGH) {
        port->bsrr = BIT(pin);
    } else if (flags & GPIO_OUTPUT_INIT_LOW) {
        port->bsrr = BIT(pin + 16);
    }
    port->otyper  = 0;
    port->pupdr   = ((flags & GPIO_PULL_UP) ? 1U : (flags & GPIO_PULL_DOWN) ? 2U : 0U) << (pin * 2U);
    port->moder   = mode << (pin * 2U);
    port->ospeedr = 0;
    port->current = 1U << (pin * 2U);
    irq_unlock(key);
    return 0;
}

static int wb32_port_get_raw(const struct device *dev, gpio_port_value_t *value) {
    const struct wb32_gpio_config *config = dev->config;
    *value                                = config->port->idr & config->common.port_pin_mask;
    return 0;
}

static int wb32_port_set_masked_raw(const struct device *dev, gpio_port_pins_t mask, gpio_port_value_t value) {
    const struct wb32_gpio_config *config = dev->config;
    mask &= config->common.port_pin_mask;
    config->port->bsrr = (mask & value) | ((mask & ~value) << 16);
    return 0;
}

static int wb32_port_set_bits_raw(const struct device *dev, gpio_port_pins_t pins) {
    return wb32_port_set_masked_raw(dev, pins, pins);
}

static int wb32_port_clear_bits_raw(const struct device *dev, gpio_port_pins_t pins) {
    return wb32_port_set_masked_raw(dev, pins, 0);
}

static int wb32_port_toggle_bits(const struct device *dev, gpio_port_pins_t pins) {
    const struct wb32_gpio_config *config = dev->config;
    unsigned int                   key    = irq_lock();
    int                            result = wb32_port_set_masked_raw(dev, pins, ~config->port->odr);
    irq_unlock(key);
    return result;
}

static int wb32_interrupt_configure(const struct device *dev, gpio_pin_t pin, enum gpio_int_mode mode, enum gpio_int_trig trigger) {
    ARG_UNUSED(dev);
    ARG_UNUSED(pin);
    ARG_UNUSED(trigger);
    return mode == GPIO_INT_MODE_DISABLED ? 0 : -ENOTSUP;
}

static int wb32_gpio_init(const struct device *dev) {
    const struct wb32_gpio_config *config = dev->config;
    sys_set_bits(WB32_RCC_BASE + 0x48, BIT(15) | BIT(config->clock_bit));
    return 0;
}

static DEVICE_API(gpio, wb32_gpio_api) = {
    .pin_configure           = wb32_pin_configure,
    .port_get_raw            = wb32_port_get_raw,
    .port_set_masked_raw     = wb32_port_set_masked_raw,
    .port_set_bits_raw       = wb32_port_set_bits_raw,
    .port_clear_bits_raw     = wb32_port_clear_bits_raw,
    .port_toggle_bits        = wb32_port_toggle_bits,
    .pin_interrupt_configure = wb32_interrupt_configure,
};

#define WB32_GPIO_DEFINE(n)                                                 \
    static struct wb32_gpio_data         wb32_gpio_data_##n;                \
    static const struct wb32_gpio_config wb32_gpio_config_##n = {           \
        .common    = {.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(n)}, \
        .port      = (void *)DT_INST_REG_ADDR(n),                           \
        .clock_bit = DT_INST_PROP(n, clock_bit),                            \
    };                                                                      \
    DEVICE_DT_INST_DEFINE(n, wb32_gpio_init, NULL, &wb32_gpio_data_##n, &wb32_gpio_config_##n, PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY, &wb32_gpio_api);
DT_INST_FOREACH_STATUS_OKAY(WB32_GPIO_DEFINE)
