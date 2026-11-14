/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#define ARG_UNUSED(value) (void)(value)
#define IS_ENABLED(value) (value)
struct k_thread {
    uint8_t id;
};
typedef struct k_thread *k_tid_t;
extern k_tid_t           test_power_thread;
extern uint32_t          test_power_now;
static inline k_tid_t    k_current_get(void) {
    return test_power_thread;
}
static inline uint32_t k_uptime_get_32(void) {
    return test_power_now;
}
static inline int64_t k_uptime_get(void) {
    return test_power_now;
}
static inline bool k_is_in_isr(void) {
    return false;
}
void k_busy_wait(uint32_t usec);
#define K_MSEC(ms) (ms)
void k_sleep(uint32_t milliseconds);
struct k_work {
    void (*handler)(struct k_work *);
};
#define K_WORK_DEFINE(name, function) struct k_work name = {.handler = function}
int k_work_submit(struct k_work *work);
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
    (void)key;
    assert(lock->held);
    lock->held = false;
}
