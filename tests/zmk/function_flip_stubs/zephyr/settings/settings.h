/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stddef.h>
#include <sys/types.h>
typedef ssize_t (*settings_read_cb)(void *, void *, size_t);
int settings_save_one(const char *name, const void *value, size_t size);
#define SETTINGS_STATIC_HANDLER_DEFINE(id, name, get, set, commit, export) static int (*const registered_set)(const char *, size_t, settings_read_cb, void *) = (set)
