#pragma once
#include "../../wired_stubs/zephyr/kernel.h"
#include <assert.h>
struct k_spinlock {
    bool held;
};
typedef unsigned       k_spinlock_key_t;
static inline unsigned k_spin_lock(struct k_spinlock *s) {
    assert(!s->held);
    s->held = true;
    return 0;
}
static inline void k_spin_unlock(struct k_spinlock *s, unsigned key) {
    (void)key;
    assert(s->held);
    s->held = false;
}
