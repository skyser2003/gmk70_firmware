// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
typedef struct {
    bool held;
} mutex_t;
#define MUTEX_DECL(name) mutex_t name
void chMtxLock(mutex_t *mutex);
void chMtxUnlock(mutex_t *mutex);
bool chMtxTryLock(mutex_t *mutex);
