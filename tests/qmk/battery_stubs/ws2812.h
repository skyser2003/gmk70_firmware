// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
enum { WS2812_LED_COUNT = 77 };
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ws2812_led_t;
