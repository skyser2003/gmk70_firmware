/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#define IRQ_CONNECT(irq, priority, handler, arg, flags) (void)(handler)
static inline void irq_enable(unsigned irq) {
    (void)irq;
}
extern unsigned        power_irq_depth;
static inline unsigned irq_lock(void) {
    return power_irq_depth++;
}
static inline void irq_unlock(unsigned key) {
    power_irq_depth = key;
}
