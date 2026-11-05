/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
struct input_event {
    int value;
};
extern void (*power_test_pointing)(struct input_event *, void *);
#define INPUT_CALLBACK_DEFINE(device, callback, context) void (*power_test_pointing)(struct input_event *, void *) = callback
