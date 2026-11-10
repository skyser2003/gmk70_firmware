/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* All low-level clock operations below require PRIMASK=1. */
bool gmk_sleep_clock_begin(void);
bool gmk_sleep_clock_checkpoint(bool *alarm);
void gmk_sleep_clock_end(void);
void gmk_sleep_clock_gate(void);
