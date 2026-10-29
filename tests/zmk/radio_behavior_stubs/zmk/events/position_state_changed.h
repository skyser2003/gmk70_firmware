/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <zephyr/device.h>
struct zmk_position_state_changed {
    uint8_t  source;
    uint32_t position;
    bool     state;
    int64_t  timestamp;
};
typedef struct {
    bool                              position_event;
    struct zmk_position_state_changed data;
} zmk_event_t;
static inline const struct zmk_position_state_changed *as_zmk_position_state_changed(const zmk_event_t *event) {
    return event->position_event ? &event->data : NULL;
}
enum { ZMK_EV_EVENT_BUBBLE = 0 };
struct zmk_listener {
    int (*callback)(const zmk_event_t *);
};
#define ZMK_LISTENER(name, function) static const struct zmk_listener name = {.callback = (function)}
#define ZMK_SUBSCRIPTION(listener, event) static const struct zmk_listener *const registered_listener = &(listener)
