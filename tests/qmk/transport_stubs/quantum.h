// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../stubs/quantum.h"
#include "host.h"
typedef struct {
    bool pressed;
} keyrecord_t;
#define HS_BAT_CABLE_PIN 7
bool gpio_read_pin(uint32_t pin);
void wait_ms(unsigned ms);
