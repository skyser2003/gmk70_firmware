/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define IS_ENABLED(value) value
#define COND_CODE_0(flag, yes, no) COND_CODE_0_SELECT(flag, yes, no)
#define COND_CODE_0_SELECT(flag, yes, no) COND_CODE_0_##flag(yes, no)
#define COND_CODE_0_0(yes, no) EXPAND yes
#define COND_CODE_0_1(yes, no) EXPAND no
#define COND_CODE_1(flag, yes, no) COND_CODE_0(flag, no, yes)
#define EXPAND(...) __VA_ARGS__
#define BIT(index) (1UL << (index))
#define WRITE_BIT(value, index, set) ((value) = (set) ? (value) | BIT(index) : (value) & ~BIT(index))
#define ARRAY_SIZE(value) (sizeof(value) / sizeof((value)[0]))
#define DIV_ROUND_UP(value, divisor) (((value) + (divisor) - 1) / (divisor))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define __packed __attribute__((packed))
