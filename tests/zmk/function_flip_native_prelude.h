#pragma once
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define IS_ENABLED(value) (value)
#define BIT(bit) (UINT32_C(1) << (bit))
#define WRITE_BIT(value, bit, enabled) ((value) = (enabled) ? ((value) | BIT(bit)) : ((value) & ~BIT(bit)))
#define Z_DECL_ALIGN(type) type
#define _CONCAT_IMPL(left, right) left##right
#define _CONCAT(left, right) _CONCAT_IMPL(left, right)
#define __used __attribute__((used))
