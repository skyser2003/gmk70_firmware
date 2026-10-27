// SPDX-License-Identifier: GPL-2.0-or-later
#include "trial_deadline.h"

enum {
    TRACE_ENABLE   = 1U << 24,
    COUNTER_ENABLE = 1,
    NO_COUNTER     = 1U << 25,
};

bool gmk_trial_deadline_begin(gmk_trial_deadline_t *deadline, const gmk_trial_mmio_t *io, uint32_t clock_hz) {
    if (!deadline) return false;
    deadline->active = false;
    // A budget below half a counter wrap keeps unsigned elapsed-time checks
    // unambiguous. Validate before multiplication or any peripheral access.
    if (!io || !io->read || !io->write || !clock_hz || clock_hz > INT32_MAX / GMK_TRIAL_DEADLINE_SECONDS) return false;
    uint32_t saved_demcr   = io->read(GMK_TRIAL_DEADLINE_DEMCR);
    uint32_t saved_control = io->read(GMK_TRIAL_DEADLINE_CONTROL);
    if (saved_control & NO_COUNTER) goto failed;
    io->write(GMK_TRIAL_DEADLINE_DEMCR, saved_demcr | TRACE_ENABLE);
    if (!(io->read(GMK_TRIAL_DEADLINE_DEMCR) & TRACE_ENABLE)) goto failed;
    io->write(GMK_TRIAL_DEADLINE_CONTROL, saved_control | COUNTER_ENABLE);
    uint32_t control = io->read(GMK_TRIAL_DEADLINE_CONTROL);
    if ((control & (COUNTER_ENABLE | NO_COUNTER)) != COUNTER_ENABLE) goto failed;
    uint32_t initial = io->read(GMK_TRIAL_DEADLINE_CYCLES);
    for (uint32_t remaining = GMK_TRIAL_DEADLINE_POLL_LIMIT; remaining; --remaining) {
        uint32_t now = io->read(GMK_TRIAL_DEADLINE_CYCLES);
        if (now == initial) continue;
        deadline->start   = now;
        deadline->last    = now;
        deadline->budget  = clock_hz * GMK_TRIAL_DEADLINE_SECONDS;
        deadline->stalled = 0;
        deadline->active  = true;
        return true;
    }
failed:
    io->write(GMK_TRIAL_DEADLINE_CONTROL, saved_control);
    io->write(GMK_TRIAL_DEADLINE_DEMCR, saved_demcr);
    return false;
}

void gmk_trial_deadline_rebase(gmk_trial_deadline_t *deadline, const gmk_trial_mmio_t *io) {
    if (!deadline || !deadline->active) return;
    if (!io || !io->read) {
        deadline->active = false;
        return;
    }
    deadline->start   = io->read(GMK_TRIAL_DEADLINE_CYCLES);
    deadline->last    = deadline->start;
    deadline->stalled = 0;
}

bool gmk_trial_deadline_expired(gmk_trial_deadline_t *deadline, const gmk_trial_mmio_t *io) {
    if (!deadline || !deadline->active || !io || !io->read) return true;
    uint32_t now = io->read(GMK_TRIAL_DEADLINE_CYCLES);
    if (now == deadline->last) {
        if (deadline->stalled < GMK_TRIAL_DEADLINE_POLL_LIMIT) ++deadline->stalled;
    } else {
        deadline->last    = now;
        deadline->stalled = 0;
    }
    return (uint32_t)(now - deadline->start) >= deadline->budget || deadline->stalled >= GMK_TRIAL_DEADLINE_POLL_LIMIT;
}
