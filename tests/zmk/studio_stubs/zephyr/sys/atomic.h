#pragma once
#include <stdatomic.h>
typedef atomic_int atomic_t;
#define atomic_get(p) atomic_load(p)
#define atomic_set(p, v) atomic_store(p, v)
#define atomic_clear(p) atomic_store(p, 0)
