/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../../radio_kernel_stubs/zephyr/kernel.h"
typedef const uint32_t *k_tid_t;
#ifndef k_current_get
static inline k_tid_t k_current_get(void) {
    return &radio_test_time;
}
#endif
#define K_FOREVER (-1)
struct k_mutex {
    unsigned depth;
};
static inline int k_mutex_init(struct k_mutex *mutex) {
    mutex->depth = 0;
    return 0;
}
static inline int k_mutex_lock(struct k_mutex *mutex, int timeout) {
    assert(timeout == K_FOREVER && !radio_test_locked);
    ++mutex->depth;
    return 0;
}
static inline int k_mutex_unlock(struct k_mutex *mutex) {
    assert(mutex->depth && !radio_test_locked);
    --mutex->depth;
    return 0;
}
