/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#define MAX(left, right) ((left) > (right) ? (left) : (right))
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define STRUCT_SECTION_FOREACH(type, iterator) for (struct type *iterator = native_##type; iterator < native_##type + 3; ++iterator)
