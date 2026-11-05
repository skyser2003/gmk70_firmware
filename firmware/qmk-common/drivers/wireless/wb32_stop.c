// Copyright 2024 Su (@isuua)
// Copyright (c) 2026 GMK70 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "wb32_stop.h"

#define SYSTICK_CTRL 0xe000e010U
#define SCB_ICSR 0xe000ed04U
#define SCB_SCR 0xe000ed10U
#define PWR_CR0 0x40010000U
#define PWR_CFGR 0x40010020U

void wb32_stop_enter(const struct wb32_stop_ops *ops) {
    ops->write32(ops->read32(SYSTICK_CTRL) & ~1U, SYSTICK_CTRL);
    ops->write32(ops->read32(SCB_ICSR) | (1U << 25), SCB_ICSR);
    /* Preserve pending EXTI/NVIC wake requests armed by the owner. With
     * PRIMASK set, a pending enabled IRQ still releases Cortex-M3 WFI. */

    /* Exact existing WB32 LP4/S32KON sequence; retain DBP and FCLKSD. */
    ops->write32(ops->read32(PWR_CR0) & 0x09U, PWR_CR0);
    ops->write32(ops->read32(PWR_CR0) | 0x3b004U, PWR_CR0);
    ops->write32(0x3b3U, PWR_CFGR);
    ops->prepare();
    ops->write32(ops->read32(SCB_SCR) | 4U, SCB_SCR);
    ops->wait();
    ops->resume();
    ops->write32(ops->read32(SCB_SCR) & ~4U, SCB_SCR);
    ops->write32(ops->read32(SYSTICK_CTRL) | 1U, SYSTICK_CTRL);
}
