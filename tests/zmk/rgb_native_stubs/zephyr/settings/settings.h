/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../../../studio_stubs/zephyr/settings/settings.h"
#define SETTINGS_STATIC_HANDLER_DEFINE(id, path, get, set, commit, export)      \
    int (*rgb_test_load)(const char *, size_t, settings_read_cb, void *) = set; \
    const struct settings_handler_static settings_handler_rgb_underglow  = {.h_set = set}
int settings_save_one(const char *name, const void *value, size_t size);
int settings_name_steq(const char *name, const char *key, const char **next);
