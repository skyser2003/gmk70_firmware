/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ARG_UNUSED(value) (void)(value)
#define IS_ENABLED(value) (value)
#define ARRAY_SIZE(value) (sizeof(value) / sizeof((value)[0]))
#define POST_KERNEL 1
#define CONFIG_KERNEL_INIT_PRIORITY_DEFAULT 40
struct device;
