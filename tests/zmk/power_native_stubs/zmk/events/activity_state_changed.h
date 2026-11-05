/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <zmk/activity.h>
struct zmk_activity_state_changed {
    enum zmk_activity_state state;
};
typedef struct {
    bool                              position;
    struct zmk_activity_state_changed activity;
} zmk_event_t;
static inline const struct zmk_activity_state_changed *as_zmk_activity_state_changed(const zmk_event_t *event) {
    return event->position ? NULL : &event->activity;
}
enum { ZMK_EV_EVENT_BUBBLE = 0 };
struct zmk_listener {
    int (*callback)(const zmk_event_t *);
};
extern const struct zmk_listener *power_test_listener;
#define ZMK_LISTENER(name, function)                                               \
    static const struct zmk_listener name                = {.callback = function}; \
    const struct zmk_listener       *power_test_listener = &name
#define ZMK_SUBSCRIPTION(name, event)
int raise_zmk_activity_state_changed(struct zmk_activity_state_changed event);
