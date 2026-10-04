// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include_next "quantum.h"
bool     is_keyboard_left(void);
void     chSysLock(void);
void     chSysUnlock(void);
uint32_t timer_read32(void);
uint32_t timer_elapsed32(uint32_t time);
