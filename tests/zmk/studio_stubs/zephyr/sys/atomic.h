#pragma once
#include <stdatomic.h>
typedef atomic_int atomic_t;
#define atomic_get(p) atomic_load(p)
#define atomic_set(p, v) atomic_store(p, v)
#define atomic_clear(p) atomic_store(p, 0)
#define atomic_and(p, v) atomic_fetch_and(p, v)
#define atomic_or(p, v) atomic_fetch_or(p, v)
