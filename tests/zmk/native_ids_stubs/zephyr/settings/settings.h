/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stddef.h>
#include <sys/types.h>
typedef ssize_t (*settings_read_cb)(void *, void *, size_t);
typedef int (*settings_load_direct_cb)(const char *, size_t, settings_read_cb, void *, void *);
struct settings_load_arg {
    const char             *subtree;
    settings_load_direct_cb cb;
    void                   *param;
};
struct native_ids_settings_handler {
    int (*set)(const char *, size_t, settings_read_cb, void *);
    int (*commit)(void);
};
extern const struct native_ids_settings_handler native_behavior_settings;
#define SETTINGS_STATIC_HANDLER_DEFINE(id, subtree, get, set_fn, commit_fn, export_fn) const struct native_ids_settings_handler native_behavior_settings = {.set = set_fn, .commit = commit_fn}
int settings_name_steq(const char *name, const char *key, const char **next);
int settings_save_one(const char *name, const void *value, size_t size);
int settings_call_set_handler(const char *name, size_t size, settings_read_cb read, void *context, const struct settings_load_arg *arg);
