/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <zmk/behavior.h>
int zmk_split_central_invoke_behavior(uint8_t source, const struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event, bool pressed);
