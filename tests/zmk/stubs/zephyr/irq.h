#pragma once
extern void (*fake_usb_isr)(const void *);
#define IRQ_CONNECT(irq, priority, handler, arg, flags) (fake_usb_isr = (handler))
static inline unsigned int irq_lock(void) {
    return 0;
}
static inline void irq_unlock(unsigned int key) {
    (void)key;
}
static inline void irq_enable(unsigned int irq) {
    (void)irq;
}
static inline void irq_disable(unsigned int irq) {
    (void)irq;
}
