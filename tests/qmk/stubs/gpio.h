// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef uint16_t pin_t;
enum { A7 = 7, B9 = 25 };

void gpio_set_pin_input(pin_t pin);
bool gpio_read_pin(pin_t pin);
