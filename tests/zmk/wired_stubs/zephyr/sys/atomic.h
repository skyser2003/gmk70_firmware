#pragma once
typedef int       atomic_t;
typedef int       atomic_val_t;
static inline int atomic_get(const atomic_t *v) {
    return *v;
}
static inline void atomic_set(atomic_t *v, int value) {
    *v = value;
}
static inline void atomic_inc(atomic_t *v) {
    ++*v;
}
