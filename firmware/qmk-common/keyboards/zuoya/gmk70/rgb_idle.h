// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>

// Main-context retirement of a completed native asynchronous RGB transfer.
void gmk70_rgb_idle_poll(bool allow_flush);
// Nonblocking clock reconciliation from the existing full-STOP wake hook.
void gmk70_rgb_idle_resume(void);
