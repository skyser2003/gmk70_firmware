/* Native deterministic scheduler/queue model; no device access. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#define ARG_UNUSED(x) (void)(x)
#define K_FOREVER (-1)
#define K_NO_WAIT 0
#define K_MSEC(x) ((int)(x))
struct k_mutex {
    unsigned locked;
};
struct k_msgq {
    uint8_t  bytes[128];
    unsigned head, tail, count;
};
#define K_MUTEX_DEFINE(name) struct k_mutex name
#define K_MSGQ_DEFINE(name, size, count, align) struct k_msgq name
#define K_THREAD_DEFINE(name, stack, fn, a, b, c, prio, opts, delay) static void *const name __attribute__((used)) = (void *)&fn
extern int64_t    fake_ms;
int               k_msgq_get(struct k_msgq *, void *, int);
int               k_msgq_put(struct k_msgq *, const void *, int);
void              k_msgq_purge(struct k_msgq *);
static inline int k_mutex_lock(struct k_mutex *m, int timeout) {
    ARG_UNUSED(timeout);
    ++m->locked;
    return 0;
}
static inline int k_mutex_unlock(struct k_mutex *m) {
    --m->locked;
    return 0;
}
static inline int64_t k_uptime_get(void) {
    return fake_ms;
}
static inline uint32_t k_uptime_get_32(void) {
    return (uint32_t)fake_ms;
}
static inline void k_busy_wait(unsigned us) {
    fake_ms += (us + 999) / 1000;
}
static inline void k_sleep(int ms) {
    fake_ms += ms;
}
static inline bool k_is_in_isr(void) {
    return false;
}
