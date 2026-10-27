// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t (*read)(uint32_t address);
    void (*write)(uint32_t address, uint32_t value);
} gmk_trial_mmio_t;

enum {
    GMK_TRIAL_WATCHDOG_ANAKEY1    = 0x40010028,
    GMK_TRIAL_WATCHDOG_ANAKEY2    = 0x4001002C,
    GMK_TRIAL_WATCHDOG_LSIENR     = 0x40010444,
    GMK_TRIAL_WATCHDOG_LSISR      = 0x40010448,
    GMK_TRIAL_WATCHDOG_KR         = 0x40010800,
    GMK_TRIAL_WATCHDOG_PR         = 0x40010804,
    GMK_TRIAL_WATCHDOG_RLR        = 0x40010808,
    GMK_TRIAL_WATCHDOG_SR         = 0x4001080C,
    GMK_TRIAL_WATCHDOG_AHBENR0    = 0x40010C3C,
    GMK_TRIAL_WATCHDOG_CLKENR     = 0x40010C64,
    GMK_TRIAL_WATCHDOG_DEBUG_CR   = 0xE0042004,
    GMK_TRIAL_WATCHDOG_POLL_LIMIT = 1000000,
};

// Runs before C runtime initialization. The adapter must use volatile MMIO;
// false always precedes the start key, which is the final write on success.
bool gmk_trial_watchdog_start(const gmk_trial_mmio_t *io);

// Reconfigure an inherited watchdog without starting a stopped watchdog.
bool gmk_trial_watchdog_recover(const gmk_trial_mmio_t *io);
// Reload only; no reads, clock changes, configuration, or start key.
bool gmk_trial_watchdog_feed(const gmk_trial_mmio_t *io);
