// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "trial_watchdog.h"

typedef struct {
    uint32_t start;
    uint32_t last;
    uint32_t budget;
    uint32_t stalled;
    bool     active;
} gmk_trial_deadline_t;

enum {
    GMK_TRIAL_DEADLINE_DEMCR      = 0xE000EDFC,
    GMK_TRIAL_DEADLINE_CONTROL    = 0xE0001000,
    GMK_TRIAL_DEADLINE_CYCLES     = 0xE0001004,
    GMK_TRIAL_DEADLINE_SECONDS    = 12,
    GMK_TRIAL_DEADLINE_POLL_LIMIT = 1024,
};

// Verify a free-running DWT cycle counter without clearing it. Failure restores
// the original DWT control and DEMCR and leaves the deadline inactive.
bool gmk_trial_deadline_begin(gmk_trial_deadline_t *deadline, const gmk_trial_mmio_t *io, uint32_t clock_hz);
// Exclude request preparation and acknowledgment from the execution deadline.
void gmk_trial_deadline_rebase(gmk_trial_deadline_t *deadline, const gmk_trial_mmio_t *io);
// A missing adapter, inactive deadline, or bounded counter stall fails closed.
bool gmk_trial_deadline_expired(gmk_trial_deadline_t *deadline, const gmk_trial_mmio_t *io);
