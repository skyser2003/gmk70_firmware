/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef int32_t            atomic_t;
typedef int32_t            atomic_val_t;
static inline atomic_val_t atomic_get(const atomic_t *value) {
    return *value;
}
static inline void atomic_set(atomic_t *value, atomic_val_t update) {
    *value = update;
}
static inline bool atomic_cas(atomic_t *value, atomic_val_t expected, atomic_val_t update) {
    if (*value != expected) return false;
    *value = update;
    return true;
}
static inline atomic_val_t atomic_clear(atomic_t *value) {
    atomic_val_t old = *value;
    *value           = 0;
    return old;
}
