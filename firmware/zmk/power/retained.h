/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Existing service owner only. Initialization/calibration never claims a
 * foreign backup domain and does not block waiting for the calibration span. */
uint32_t gmk_retained_poll(void);
bool     gmk_retained_available(void);
/* Called only after native devices, peers and report producers are quiescent.
 * Returns with normal clocks/IRQs, preserving the caller's scheduler lock. */
bool gmk_retained_sleep(void);
