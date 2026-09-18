// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "host_driver.h"

bool     is_keyboard_master(void);
uint32_t sync_timer_read32(void);
uint32_t sync_timer_elapsed32(uint32_t last);
