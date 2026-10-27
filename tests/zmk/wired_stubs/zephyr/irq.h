#pragma once
extern void (*fake_uart_isr)(const void *);
#define IRQ_CONNECT(irq, priority, handler, arg, flags) (fake_uart_isr = (handler))
static inline unsigned irq_lock(void) {
    return 0;
}
static inline void irq_unlock(unsigned key) {
    (void)key;
}
static inline void irq_enable(unsigned irq) {
    (void)irq;
}
