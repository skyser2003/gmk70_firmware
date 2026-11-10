/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#ifdef CONFIG_GMK70_EVENT_IDLE
#    include <stddef.h>
#    include <errno.h>
#    define K_NO_WAIT 0
struct k_msgq {
    uint8_t  bytes[128];
    unsigned head, tail, count;
};
#    define K_MSGQ_DEFINE(name, size, count, align) struct k_msgq name
int                    k_msgq_get(struct k_msgq *, void *, int);
int                    k_msgq_put(struct k_msgq *, const void *, int);
void                   k_msgq_purge(struct k_msgq *);
static inline unsigned k_msgq_num_used_get(struct k_msgq *q) {
    return q->count;
}
#endif
#define ARG_UNUSED(value) (void)(value)
extern uint32_t battery_test_time;
extern unsigned battery_test_locks;
struct k_spinlock {
    bool held;
};
typedef unsigned       k_spinlock_key_t;
static inline uint32_t k_uptime_get_32(void) {
    return battery_test_time;
}
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held && !battery_test_locks);
    lock->held = true;
    ++battery_test_locks;
    return 0;
}
static inline void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    assert(key == 0 && lock->held && battery_test_locks == 1);
    lock->held = false;
    --battery_test_locks;
}
