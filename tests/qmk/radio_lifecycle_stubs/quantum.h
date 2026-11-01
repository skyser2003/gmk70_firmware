// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include_next "quantum.h"
#include "action.h"
#include "host.h"
bool gpio_read_pin(uint32_t pin);
void suspend_wakeup_init(void);
