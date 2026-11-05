// Copyright 2024 Su (@isuua)
// Copyright (c) 2026 GMK70 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

/* The owner must arm wake sources and mask interrupts before entry, and restore
 * clocks/pins after return. This primitive never acknowledges EXTI/NVIC edges.
 * Hooks make the exact register sequence executable in native MMIO tests. */
struct wb32_stop_ops {
    uint32_t (*read32)(uintptr_t address);
    void (*write32)(uint32_t value, uintptr_t address);
    void (*prepare)(void);
    void (*wait)(void);
    void (*resume)(void);
};

void wb32_stop_enter(const struct wb32_stop_ops *ops);
void wb32_stop_hardware(void);
