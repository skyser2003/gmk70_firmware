/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <zephyr/drivers/usb/usb_dc.h>
enum zmk_usb_conn_state { ZMK_USB_CONN_NONE, ZMK_USB_CONN_POWERED, ZMK_USB_CONN_HID };
enum zmk_usb_conn_state zmk_usb_get_conn_state(void);
enum usb_dc_status_code zmk_usb_get_status(void);
static inline bool      zmk_usb_is_powered(void) {
    return zmk_usb_get_conn_state() != ZMK_USB_CONN_NONE;
}
