// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "trial_watchdog.h"

// Read the board's B9 strap as left=0/right=1 without initialized framework
// state. Caller serializes access; every changed clock/configuration is restored.
bool gmk_trial_role_read(const gmk_trial_mmio_t *io, uint32_t *role);
