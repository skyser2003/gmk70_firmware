/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <zmk/events/position_state_changed.h>
#ifdef RADIO_NATIVE_SOURCE
/* The native keymap event loop is outside this fixture's replacement boundary. */
#    undef ZMK_LISTENER
#    undef ZMK_SUBSCRIPTION
#    define ZMK_LISTENER(name, listener)
#    define ZMK_SUBSCRIPTION(name, event)
#endif
