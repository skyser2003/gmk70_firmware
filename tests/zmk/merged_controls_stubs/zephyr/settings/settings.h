/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
struct gmk_test_settings_handler {
    const char *name;
    int         priority;
    int (*commit)(void);
};
#define SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO(hname, tree, get, set, cb, export, prio) static const struct gmk_test_settings_handler test_settings_handler = {.name = (tree), .priority = (prio), .commit = (cb)}
