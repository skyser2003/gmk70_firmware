/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
/* GPIO port index A=0..D=3; port=255 denotes the maintained null column. */
struct gmk_wired_pin {
    uint8_t port, pin;
};
