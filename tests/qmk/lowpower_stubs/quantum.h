// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../stubs/quantum.h"

uint32_t last_input_activity_elapsed(void);
void     chSysLock(void);
void     chSysUnlock(void);
void     chSysDisable(void);
void     chSysEnable(void);
void     suspend_power_down(void);
void     suspend_wakeup_init(void);
bool     rgb_matrix_is_enabled(void);
void     rgb_matrix_disable_noeeprom(void);
void     rgb_matrix_enable_noeeprom(void);
