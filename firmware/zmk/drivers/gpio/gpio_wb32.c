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
    sys_slist_t             callbacks;
};

/* WB32 CMSIS EXTI/AFIO layout and IRQ numbers, from the pinned vendor headers.
 * A numbered EXTI line can belong to only one GPIO port at a time. */
#define EXTI_IMR 0x40001800U
#define EXTI_EMR 0x40001804U
#define EXTI_RTSR 0x40001808U
#define EXTI_FTSR 0x4000180cU
#define EXTI_PR 0x40001814U
#define AFIO_EXTICR(pin) (0x40001408U + ((pin) / 4U) * 4U)
static const struct device *exti_owner[16];
static bool                 exti_initialized;

static void wb32_exti_isr(const void *arg) {
    uint32_t pending = sys_read32(EXTI_PR) & sys_read32(EXTI_IMR) & (uintptr_t)arg;
    /* W1C only this group's observed GPIO lines, before invoking callbacks. */
    sys_write32(pending, EXTI_PR);
    for (unsigned pin = 0; pin < 16; ++pin) {
        const struct device *dev = exti_owner[pin];
        if ((pending & BIT(pin)) && dev) {
            struct wb32_gpio_data *data = dev->data;
            gpio_fire_callbacks(&data->callbacks, dev, BIT(pin));
        }
    }
}

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
    if (pin >= 16) return -EINVAL;
    /* EXTI detects edges; do not advertise level-trigger emulation or STOP. */
    if (mode != GPIO_INT_MODE_DISABLED && (mode != GPIO_INT_MODE_EDGE || (trigger != GPIO_INT_TRIG_LOW && trigger != GPIO_INT_TRIG_HIGH && trigger != GPIO_INT_TRIG_BOTH))) return -ENOTSUP;
    unsigned key = irq_lock();
    uint32_t bit = BIT(pin);
    if (exti_owner[pin] && exti_owner[pin] != dev) {
        irq_unlock(key);
        return mode == GPIO_INT_MODE_DISABLED ? 0 : -EBUSY;
    }
    sys_clear_bits(EXTI_IMR, bit);
    sys_clear_bits(EXTI_EMR, bit);
    sys_clear_bits(EXTI_RTSR, bit);
    sys_clear_bits(EXTI_FTSR, bit);
    sys_write32(bit, EXTI_PR);
    exti_owner[pin] = NULL;
    if (mode != GPIO_INT_MODE_DISABLED) {
        const struct wb32_gpio_config *config = dev->config;
        uint32_t                       port   = ((uintptr_t)config->port - WB32_GPIOA_BASE) / 0x400U;
        unsigned                       shift  = (pin % 4U) * 4U;
        uintptr_t                      reg    = AFIO_EXTICR(pin);
        sys_write32((sys_read32(reg) & ~(0xfU << shift)) | (port << shift), reg);
        if (trigger & GPIO_INT_TRIG_HIGH) sys_set_bits(EXTI_RTSR, bit);
        if (trigger & GPIO_INT_TRIG_LOW) sys_set_bits(EXTI_FTSR, bit);
        exti_owner[pin] = dev;
        sys_set_bits(EXTI_IMR, bit);
    }
    irq_unlock(key);
    return 0;
}

static int wb32_manage_callback(const struct device *dev, struct gpio_callback *callback, bool set) {
    struct wb32_gpio_data *data   = dev->data;
    unsigned               key    = irq_lock();
    int                    result = gpio_manage_callback(&data->callbacks, callback, set);
    irq_unlock(key);
    return result;
}

static int wb32_gpio_init(const struct device *dev) {
    const struct wb32_gpio_config *config = dev->config;
    sys_set_bits(WB32_RCC_BASE + 0x48, BIT(15) | BIT(config->clock_bit) | BIT(9) | BIT(10));
    if (!exti_initialized) {
        sys_clear_bits(EXTI_IMR, 0xffffU);
        sys_clear_bits(EXTI_EMR, 0xffffU);
        sys_clear_bits(EXTI_RTSR, 0xffffU);
        sys_clear_bits(EXTI_FTSR, 0xffffU);
        sys_write32(0xffffU, EXTI_PR);
        IRQ_CONNECT(6, 2, wb32_exti_isr, (const void *)BIT(0), 0);
        IRQ_CONNECT(7, 2, wb32_exti_isr, (const void *)BIT(1), 0);
        IRQ_CONNECT(8, 2, wb32_exti_isr, (const void *)BIT(2), 0);
        IRQ_CONNECT(9, 2, wb32_exti_isr, (const void *)BIT(3), 0);
        IRQ_CONNECT(10, 2, wb32_exti_isr, (const void *)BIT(4), 0);
        IRQ_CONNECT(16, 2, wb32_exti_isr, (const void *)0x3e0U, 0);
        IRQ_CONNECT(33, 2, wb32_exti_isr, (const void *)0xfc00U, 0);
        irq_enable(6);
        irq_enable(7);
        irq_enable(8);
        irq_enable(9);
        irq_enable(10);
        irq_enable(16);
        irq_enable(33);
        exti_initialized = true;
    }
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
    .manage_callback         = wb32_manage_callback,
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
