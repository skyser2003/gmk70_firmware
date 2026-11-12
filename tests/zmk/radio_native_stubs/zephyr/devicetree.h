/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#define DT_NODE_EXISTS(node) 1
#ifndef TEST_FACTORY_RESET
#    define DT_FOREACH_CHILD(node, fn) fn(0) fn(1) fn(2)
#else
#    define DT_FOREACH_CHILD(node, fn) fn(0) fn(1) fn(2) fn(3)
#endif
#ifndef TEST_FACTORY_RESET
#    define DT_FOREACH_CHILD_SEP(node, fn, sep) fn(0) EXPAND sep fn(1) EXPAND sep fn(2)
#else
#    define DT_FOREACH_CHILD_SEP(node, fn, sep) fn(0) EXPAND sep fn(1) EXPAND sep fn(2) EXPAND sep fn(3)
#endif
#ifndef TEST_FACTORY_RESET
#    define DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, fn) fn(0) fn(1)
#else
#    define DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, fn) fn(0) fn(1) fn(2) fn(3)
#endif
#define DT_NODE_HAS_PROP(node, prop) 1
#define DT_PROP_LEN(node, prop) 1
#define LISTIFY(count, fn, sep, node) fn(0, node)
#define DT_PROP_OR(node, prop, fallback) "stock"
#define DEVICE_DT_NAME(node) "stock"
#define DT_PHA_HAS_CELL_AT_IDX(node, prop, idx, cell) 0
