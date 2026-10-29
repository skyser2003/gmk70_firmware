/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
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
