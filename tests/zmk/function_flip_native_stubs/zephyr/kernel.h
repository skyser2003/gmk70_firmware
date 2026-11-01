/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#define ARG_UNUSED(value) (void)(value)
#define K_FOREVER (-1)
extern uint32_t               radio_test_time;
extern _Thread_local unsigned radio_test_locked;
typedef pthread_t             k_tid_t;
static inline k_tid_t         k_current_get(void) {
    return pthread_self();
}
struct k_spinlock {
    atomic_bool held;
};
typedef unsigned               k_spinlock_key_t;
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!radio_test_locked);
    while (atomic_exchange(&lock->held, true)) {
    }
    ++radio_test_locked;
    return 0;
}
static inline void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    assert(key == 0 && radio_test_locked == 1);
    --radio_test_locked;
    atomic_store(&lock->held, false);
}
struct k_mutex {
    pthread_mutex_t native;
};
static inline int k_mutex_init(struct k_mutex *mutex) {
    pthread_mutexattr_t attributes;
    assert(pthread_mutexattr_init(&attributes) == 0);
    assert(pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE) == 0);
    assert(pthread_mutex_init(&mutex->native, &attributes) == 0);
    assert(pthread_mutexattr_destroy(&attributes) == 0);
    return 0;
}
static inline int k_mutex_lock(struct k_mutex *mutex, int timeout) {
    assert(timeout == K_FOREVER && !radio_test_locked);
    return -pthread_mutex_lock(&mutex->native);
}
static inline int k_mutex_unlock(struct k_mutex *mutex) {
    assert(!radio_test_locked);
    return -pthread_mutex_unlock(&mutex->native);
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
