/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define ARG_UNUSED(value) (void)(value)
extern uint32_t radio_test_time;
extern unsigned radio_test_locked;
struct k_spinlock {
    bool held;
};
typedef unsigned               k_spinlock_key_t;
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!radio_test_locked && !lock->held);
    lock->held = true;
    ++radio_test_locked;
    return 0;
}
static inline void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    assert(key == 0 && radio_test_locked == 1 && lock->held);
    --radio_test_locked;
    lock->held = false;
}
static inline uint32_t k_uptime_get_32(void) {
    return radio_test_time;
}
static inline int64_t k_uptime_get(void) {
    return radio_test_time;
}
struct k_work {
    void (*handler)(struct k_work *);
    bool pending;
};
static inline void k_work_init(struct k_work *work, void (*handler)(struct k_work *)) {
    work->handler = handler;
    work->pending = false;
}
static inline int k_work_submit(struct k_work *work) {
    assert(!radio_test_locked);
    work->pending = true;
    return 1;
}
