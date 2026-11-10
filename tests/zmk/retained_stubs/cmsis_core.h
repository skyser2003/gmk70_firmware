/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <fixture.h>
typedef struct {
    volatile uint32_t CTRL, LOAD, VAL, CALIB;
} SysTick_Type;
#define SysTick ((SysTick_Type *)0xe000e010UL)
#define SysTick_CTRL_ENABLE_Msk 1U
#define SysTick_CTRL_TICKINT_Msk 2U
#define SysTick_CTRL_CLKSOURCE_Msk 4U
#define SysTick_CTRL_COUNTFLAG_Msk (1U << 16)
