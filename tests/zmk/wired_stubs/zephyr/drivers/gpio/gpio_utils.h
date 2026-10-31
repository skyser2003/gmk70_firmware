#pragma once
#include <zephyr/drivers/gpio.h>
static inline int gpio_manage_callback(sys_slist_t *list, struct gpio_callback *cb, bool set) {
    struct gpio_callback **slot = &list->head;
    while (*slot && *slot != cb)
        slot = &(*slot)->next;
    if (*slot) *slot = cb->next;
    if (set) {
        cb->next   = list->head;
        list->head = cb;
    }
    return 0;
}
static inline void gpio_fire_callbacks(sys_slist_t *list, const struct device *dev, gpio_port_pins_t pins) {
    for (struct gpio_callback *cb = list->head, *next; cb; cb = next) {
        next = cb->next;
        if (cb->pin_mask & pins) cb->handler(dev, cb, pins & cb->pin_mask);
    }
}
