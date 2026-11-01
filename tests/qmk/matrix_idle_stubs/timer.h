// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
typedef uint16_t fast_timer_t;
uint16_t         timer_read_fast(void);
uint16_t         timer_elapsed_fast(uint16_t time);
uint32_t         timer_read32(void);
uint32_t         timer_elapsed32(uint32_t time);
