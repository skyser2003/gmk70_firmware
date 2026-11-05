/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <string.h>
static inline int gmk_test_settings_name_steq(const char *name, const char *key, const char **next) {
    size_t length = strlen(key);
    if (strncmp(name, key, length) || (name[length] && name[length] != '/')) return 0;
    if (next) *next = name[length] ? name + length + 1 : NULL;
    return 1;
}
