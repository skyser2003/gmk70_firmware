// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hal.h"
// Upstream's SPIConfig initializer requires constant pin expressions.
#define PAL_PORT(pin) 0
#define PAL_PAD(pin) 15
#define PAL_MODE_ALTERNATE(mode) (mode)
#define PAL_OUTPUT_TYPE_PUSHPULL 0
#define PAL_OUTPUT_TYPE_OPENDRAIN 0x100U
void palSetLineMode(unsigned pin, unsigned mode);
void gpio_write_pin_low(unsigned pin);
void gpio_set_pin_output(unsigned pin);
