// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "config.h"
typedef uint16_t pin_t;
typedef uint16_t ioline_t;
bool             gpio_read_pin(pin_t pin);
void             gpio_write_pin_low(pin_t pin);
void             gpio_write_pin_high(pin_t pin);
void             gpio_set_pin_output(pin_t pin);
void             gpio_set_pin_output_open_drain(pin_t pin);
void             gpio_set_pin_input_high(pin_t pin);
void             waitInputPinDelay(void);
