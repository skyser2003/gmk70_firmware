/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <zmk/events/activity_state_changed.h>
static inline bool as_zmk_position_state_changed(const zmk_event_t *event) {
    return event->position;
}
