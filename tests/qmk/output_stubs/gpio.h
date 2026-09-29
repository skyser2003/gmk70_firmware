// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hal.h"
// Upstream's SPIConfig initializer requires constant pin expressions.
#define PAL_PORT(pin) 0
#define PAL_PAD(pin) 15
#define PAL_MODE_ALTERNATE(mode) (mode)
#define PAL_OUTPUT_TYPE_PUSHPULL 0
void palSetLineMode(unsigned pin, unsigned mode);
