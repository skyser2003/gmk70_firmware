// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool locked;
} mutex_t;
#define __MUTEX_DATA(name) {false}
void chMtxLock(mutex_t *mutex);
void chMtxUnlock(mutex_t *mutex);
