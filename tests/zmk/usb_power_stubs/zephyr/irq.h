/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
extern unsigned        power_irq_depth;
static inline unsigned irq_lock(void) {
    return power_irq_depth++;
}
static inline void irq_unlock(unsigned key) {
    power_irq_depth = key;
}
