/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#define K_MSEC(value) (value)
struct k_spinlock {
    bool held;
};
typedef unsigned               k_spinlock_key_t;
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held);
    lock->held = true;
    return 0;
}
static inline void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    assert(lock->held && !key);
    lock->held = false;
}
struct k_work {
    void (*handler)(struct k_work *);
};
struct k_work_delayable {
    struct k_work work;
    uint64_t      due;
};
static struct k_work_delayable *recovery_test_timer;
static uint64_t                 recovery_test_now;
static inline uint32_t          k_uptime_get_32(void) {
    return (uint32_t)recovery_test_now;
}
#define K_WORK_DELAYABLE_DEFINE(name, callback) struct k_work_delayable name = {.work = {.handler = callback}}
static inline int k_work_reschedule(struct k_work_delayable *work, uint32_t ms) {
    work->due           = recovery_test_now + ms;
    recovery_test_timer = work;
    return 1;
}
static inline int k_work_cancel_delayable(struct k_work_delayable *work) {
    if (recovery_test_timer == work) recovery_test_timer = 0;
    return 0;
}
static inline void recovery_test_advance(uint32_t ms) {
    recovery_test_now += ms;
    if (recovery_test_timer && recovery_test_now >= recovery_test_timer->due) {
        struct k_work_delayable *work = recovery_test_timer;
        recovery_test_timer           = 0;
        work->work.handler(&work->work);
    }
}
bool k_is_in_isr(void);
