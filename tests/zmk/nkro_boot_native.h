/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <zmk/hid.h>

// The native bodies retain their counter, bitmap, and boot packing logic.
// Replace only Zephyr's platform macros and compile-time configuration.
#define IS_ENABLED(value) (value)
#define BIT(bit) (UINT32_C(1) << (bit))
#define WRITE_BIT(value, bit, enabled) ((value) = (enabled) ? ((value) | BIT(bit)) : ((value) & ~BIT(bit)))
#define LOG_DBG(...)
#define LOG_ERR(...)
#define ZMK_HID_REPORT_ID_KEYBOARD 1
#define ZMK_HID_REPORT_ID_CONSUMER 2
#define ZMK_HID_KEYBOARD_NKRO_MAX_USAGE HID_USAGE_KEY_KEYBOARD_LANG8

int zmk_hid_keyboard_press(uint32_t usage);
int zmk_hid_keyboard_release(uint32_t usage);
