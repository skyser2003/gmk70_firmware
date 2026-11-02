/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include "../../../radio_native_stubs/zephyr/sys/util.h"
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define DT_NODE_HAS_STATUS(node, status) 0
#define Z_DECL_ALIGN(type) type
#define STRINGIFY(value) #value
#define _CONCAT(a, b) CONCAT_IMPL(a, b)
#define CONCAT_IMPL(a, b) a##b
#define __used __attribute__((used))
#define CONTAINER_OF(pointer, type, member) ((type *)((char *)(pointer) - offsetof(type, member)))
#define STRUCT_SECTION_ITERABLE_NAMED(type, section, name) struct type name
struct zmk_split_transport_central;
struct zmk_split_transport_peripheral;
const struct zmk_split_transport_central    *test_first_zmk_split_transport_central(void);
const struct zmk_split_transport_peripheral *test_first_zmk_split_transport_peripheral(void);
#define STRUCT_SECTION_FOREACH(type, name) for (const struct type *name = test_first_##type(); name; name = NULL)
