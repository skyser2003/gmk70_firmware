/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stddef.h>
#include <sys/types.h>
typedef ssize_t (*settings_read_cb)(void *, void *, size_t);
typedef int (*settings_load_direct_cb)(const char *, size_t, settings_read_cb, void *, void *);
struct radio_native_settings_handler {
    int (*set)(const char *, size_t, settings_read_cb, void *);
    int (*commit)(void);
};
extern const struct radio_native_settings_handler radio_native_settings;
#define SETTINGS_STATIC_HANDLER_DEFINE(name, subtree, get, set_fn, commit_fn, export_fn) const struct radio_native_settings_handler radio_native_settings = {.set = set_fn, .commit = commit_fn}
int settings_load_subtree(const char *subtree);
int settings_load_subtree_direct(const char *subtree, settings_load_direct_cb callback, void *param);
int settings_delete(const char *key);
int settings_save_one(const char *key, const void *value, size_t len);
int settings_name_steq(const char *name, const char *key, const char **next);
