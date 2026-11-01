/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#define APPLICATION 1
#define POST_KERNEL 1
#ifdef RADIO_NATIVE_SOURCE
/* The fixture calls keymap_init explicitly. */
#    define SYS_INIT(function, level, priority)
#else
#    define SYS_INIT(function, level, priority) static int (*const registered_init)(void) = (function)
#endif
