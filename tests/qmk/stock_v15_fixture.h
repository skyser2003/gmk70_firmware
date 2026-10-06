// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "stock_v15.h"
typedef struct {
    unsigned activity, reset, boot;
    uint8_t  sleep, nkro, brightness;
} stock_v15_fixture_t;
extern stock_v15_fixture_t stock_v15_fixture;
