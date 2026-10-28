#pragma once
#include <stdint.h>
#include <zephyr/device.h>
int                uart_irq_update(const struct device *);
int                uart_irq_rx_ready(const struct device *);
int                uart_irq_tx_ready(const struct device *);
int                uart_fifo_read(const struct device *, uint8_t *, int);
int                uart_fifo_fill(const struct device *, const uint8_t *, int);
static inline void uart_irq_tx_enable(const struct device *d) {
    (void)d;
}
static inline void uart_irq_rx_enable(const struct device *d) {
    (void)d;
}
static inline void uart_irq_rx_disable(const struct device *d) {
    (void)d;
}
static inline int uart_irq_callback_user_data_set(const struct device *d, void (*cb)(const struct device *, void *), void *ctx) {
    (void)d;
    (void)cb;
    (void)ctx;
    return 0;
}
