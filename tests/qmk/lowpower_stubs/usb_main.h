// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t state;
} test_usb_driver_t;

extern test_usb_driver_t USB_DRIVER;
enum { USB_ACTIVE = 1 };
