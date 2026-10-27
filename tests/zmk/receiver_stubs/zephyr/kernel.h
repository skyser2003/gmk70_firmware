/* Native lock model: detects external work under the critical section. */
#pragma once
#include <assert.h>
struct k_spinlock {
    unsigned held;
};
typedef unsigned               k_spinlock_key_t;
extern unsigned                receiver_locked;
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held);
    lock->held = 1;
    ++receiver_locked;
    return 0;
}
static inline void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    (void)key;
    assert(lock->held);
    lock->held = 0;
    --receiver_locked;
}
#include <errno.h>
#include <stdint.h>
#include <string.h>
#define ARG_UNUSED(x) (void)(x)
#define K_NO_WAIT 0
struct k_msgq {
    uint8_t  data[4][32];
    unsigned head, count;
};
#define K_MSGQ_DEFINE(name, size, count, align) struct k_msgq name
static inline int k_msgq_put(struct k_msgq *q, const void *data, int timeout) {
    (void)timeout;
    if (q->count == 4) return -ENOSPC;
    memcpy(q->data[(q->head + q->count) % 4], data, 32);
    ++q->count;
    return 0;
}
static inline int k_msgq_get(struct k_msgq *q, void *data, int timeout) {
    (void)timeout;
    if (!q->count) return -ENOMSG;
    memcpy(data, q->data[q->head], 32);
    q->head = (q->head + 1) % 4;
    --q->count;
    return 0;
}
#define K_FOREVER (-1)
#define K_MSEC(x) (x)
#define K_THREAD_DEFINE(name, stack, fn, a, b, c, prio, opts, delay) static void *name = (void *)&fn
void     k_thread_start(void *);
void     k_sleep(int);
uint32_t k_uptime_get_32(void);
#define SYS_FOREVER_MS (-1)
