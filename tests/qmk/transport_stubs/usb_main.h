// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
typedef struct {
    uint8_t state, status;
} test_usb_driver_t;
extern test_usb_driver_t USB_DRIVER;
enum { USB_STOP = 1, USB_READY = 2, USB_ACTIVE = 4, USB_SUSPENDED = 5 };
void usbWakeupHost(test_usb_driver_t *driver);
