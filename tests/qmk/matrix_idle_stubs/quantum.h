// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "matrix.h"
#include "timer.h"
#include <stddef.h>
typedef struct {
    void (*cb)(void *);
    void *arg;
} palevent_t;
typedef struct {
    uint32_t IMR, RTSR, FTSR;
} test_exti_t;
extern palevent_t  _pal_events[16];
extern test_exti_t test_exti;
#define EXTI (&test_exti)
#define PAL_PAD(pin) ((pin) % 16)
#define PAL_EVENT_MODE_FALLING_EDGE 2
void chSysLock(void);
void chSysUnlock(void);
void palEnableLineEventI(pin_t pin, uint32_t edges);
void palDisableLineEventI(pin_t pin);
void nvicEnableVector(unsigned vector, unsigned priority);
