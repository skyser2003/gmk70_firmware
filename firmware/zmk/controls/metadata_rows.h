/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
/* One source for native enum values and human-readable Studio labels. */

#define GMK70_BASE_CONTROL_OPTIONS(X)                              \
    X(GMK70_GUI_TOGGLE, "GUI lock: toggle Win/Command keys")       \
    X(GMK70_GUI_ON, "GUI lock: block Win/Command keys")            \
    X(GMK70_GUI_OFF, "GUI lock: allow Win/Command keys")           \
    X(GMK70_OS_WINDOWS, "Windows mode: normal Alt/GUI")            \
    X(GMK70_OS_MAC, "Mac mode: swap Alt/GUI")                      \
    X(GMK70_OS_TOGGLE, "Windows/Mac mode: toggle")                 \
    X(GMK70_USB_SLEEP_TOGGLE, "USB idle sleep: toggle permission") \
    X(GMK70_USB_SLEEP_ON, "USB idle sleep: allow saved timeout")   \
    X(GMK70_USB_SLEEP_OFF, "USB idle sleep: keep USB awake")

#define GMK70_EXTRA_CONTROL_OPTIONS(X)                                    \
    X(GMK70_FUNCTION_FLIP, "Flip: numbers to F-keys/Mac media")           \
    X(GMK70_NKRO_TOGGLE, "NKRO: toggle multi-key rollover")               \
    X(GMK70_NKRO_ON, "NKRO: on (more than 6 keys)")                       \
    X(GMK70_NKRO_OFF, "NKRO: off (6-key compatibility)")                  \
    X(GMK70_RECOVERY_RESET, "Reboot this half: hold 3s/release")          \
    X(GMK70_RECOVERY_BOOTLOADER, "Bootloader this half: hold 3s/release") \
    X(GMK70_RECOVERY_SETTINGS_RESET, "Clear app settings: hold 3s, either half")

#define GMK70_SLEEP_OPTIONS(X) \
    X(0, "Never")              \
    X(1, "1 min")              \
    X(2, "3 min")              \
    X(3, "5 min")              \
    X(4, "10 min")             \
    X(5, "20 min")             \
    X(6, "30 min")             \
    X(7, "Default: 5 min")

#define GMK70_RADIO_OPTIONS(X)    \
    X(GMK_HOST_USB, "USB")        \
    X(GMK_HOST_BT1, "Bluetooth1") \
    X(GMK_HOST_BT2, "Bluetooth2") \
    X(GMK_HOST_BT3, "Bluetooth3") \
    X(GMK_HOST_BT4, "Bluetooth4") \
    X(GMK_HOST_BT5, "Bluetooth5") \
    X(GMK_HOST_DONGLE, "Dongle")

#define GMK70_BATTERY_OPTIONS(X)           \
    X(GMK_BATTERY_LEVEL, "Battery (hold)") \
    X(GMK_BATTERY_CHARGING, "Charging (hold)")
