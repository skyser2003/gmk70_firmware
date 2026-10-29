/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#define POST_KERNEL 1
#define SYS_INIT(function, level, priority) static int (*const registered_init)(void) = (function)
