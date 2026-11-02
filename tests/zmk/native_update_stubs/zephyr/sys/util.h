/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "../../../native_encoder_stubs/zephyr/sys/util.h"
#define IS_ENABLED(value) value
#define DT_NODE_HAS_STATUS(node, status) 0
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define unlikely(value) (value)
#define ARG_UNUSED(x) (void)(x)
#define __packed __attribute__((packed))
#define __ASSERT(condition, ...) assert(condition)
#define __ASSERT_NO_MSG(condition) assert(condition)
#define __ASSERT_EVAL(disabled, enabled, condition, ...) \
    do {                                                 \
        enabled;                                         \
        assert(condition);                               \
    } while (0)
