// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

void     stock_power_key(bool pressed);
bool     stock_power_prepare_allowed(bool critical_battery);
uint32_t stock_power_timeout(uint8_t selector);
