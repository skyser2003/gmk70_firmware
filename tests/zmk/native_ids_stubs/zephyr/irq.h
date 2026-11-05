/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <assert.h>
extern unsigned        native_ids_irq_depth;
static inline unsigned irq_lock(void) {
    assert(native_ids_irq_depth == 0);
    native_ids_irq_depth = 1;
    return 0;
}
static inline void irq_unlock(unsigned key) {
    assert(key == 0 && native_ids_irq_depth == 1);
    native_ids_irq_depth = 0;
}
