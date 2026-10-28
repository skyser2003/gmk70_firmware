#pragma once
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define ARG_UNUSED(x) (void)(x)
#define K_FOREVER (-1)
#define K_MUTEX_DEFINE(n) pthread_mutex_t n = PTHREAD_MUTEX_INITIALIZER
#define k_mutex_lock(m, t) ((void)(t), pthread_mutex_lock(m))
#define k_mutex_unlock(m) pthread_mutex_unlock(m)
struct k_sem {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    unsigned        count;
};
#define K_SEM_DEFINE(n, initial, limit) struct k_sem n = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, initial}
static inline void k_sem_take(struct k_sem *s, int timeout) {
    (void)timeout;
    pthread_mutex_lock(&s->mutex);
    while (!s->count)
        pthread_cond_wait(&s->cond, &s->mutex);
    --s->count;
    pthread_mutex_unlock(&s->mutex);
}
static inline void k_sem_give(struct k_sem *s) {
    pthread_mutex_lock(&s->mutex);
    assert(!s->count);
    ++s->count;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex);
}
struct k_spinlock {
    atomic_bool held;
};
typedef unsigned       k_spinlock_key_t;
static inline unsigned k_spin_lock(struct k_spinlock *s) {
    while (atomic_exchange(&s->held, true)) {
    }
    return 0;
}
static inline void k_spin_unlock(struct k_spinlock *s, unsigned key) {
    (void)key;
    atomic_store(&s->held, false);
}
