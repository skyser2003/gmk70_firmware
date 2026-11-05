/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stddef.h>
#include <sys/types.h>
typedef ssize_t (*settings_read_cb)(void *, void *, size_t);
struct controls_test_settings_handler {
    int (*set)(const char *, size_t, settings_read_cb, void *);
    int (*commit)(void);
};
#define SETTINGS_STATIC_HANDLER_DEFINE(id, subtree, get, set_fn, commit_fn, export_fn) static const struct controls_test_settings_handler controls_test_settings = {.set = set_fn, .commit = commit_fn}
