/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

/* Values are shared by saved native bindings and the keymap migration tool. */
#define GMK70_GUI_TOGGLE 0
#define GMK70_GUI_ON 1
#define GMK70_GUI_OFF 2
#define GMK70_OS_WINDOWS 3
#define GMK70_OS_MAC 4
#define GMK70_OS_TOGGLE 5
#define GMK70_USB_SLEEP_TOGGLE 6
#define GMK70_USB_SLEEP_ON 7
#define GMK70_USB_SLEEP_OFF 8

/* Preserve 0..8 for existing saved bindings. */
#define GMK70_FUNCTION_FLIP 9
#define GMK70_NKRO_TOGGLE 10
#define GMK70_NKRO_ON 11
#define GMK70_NKRO_OFF 12
#define GMK70_RECOVERY_RESET 13
#define GMK70_RECOVERY_BOOTLOADER 14
#define GMK70_RECOVERY_SETTINGS_RESET 15
