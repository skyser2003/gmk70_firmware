/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
extern unsigned int startup_irq_depth;
extern void (*fake_usb_isr)(const void *);
#define IRQ_CONNECT(irq, priority, handler, arg, flags) (fake_usb_isr = (handler))
static inline unsigned int irq_lock(void) {
    return startup_irq_depth++;
}
static inline void irq_unlock(unsigned int key) {
    startup_irq_depth = key;
}
static inline void irq_enable(unsigned int irq) {
    (void)irq;
}
static inline void irq_disable(unsigned int irq) {
    (void)irq;
}
