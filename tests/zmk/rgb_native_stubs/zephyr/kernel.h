/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../../studio_stubs/zephyr/kernel.h"
#include <errno.h>
#define IS_ENABLED(value) (value)
#define BUILD_ASSERT(test, message) _Static_assert(test, message)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(v, low, high) ((v) < (low) ? (low) : (v) > (high) ? (high) : (v))
#define K_MSEC(value) (value)
#define K_NO_WAIT 0
extern uint32_t rgb_test_time;
struct k_work {
    void (*handler)(struct k_work *);
    bool pending;
};
struct k_work_delayable {
    struct k_work work;
    uint32_t      due;
};
struct k_timer {
    void (*handler)(struct k_timer *);
};
#define K_WORK_DEFINE(name, callback) struct k_work name = {.handler = callback}
#define K_TIMER_DEFINE(name, callback, stop) struct k_timer name = {.handler = callback}
uint32_t k_uptime_get_32(void);
int      k_work_submit(struct k_work *work);
int      k_work_submit_to_queue(void *queue, struct k_work *work);
void     k_work_init_delayable(struct k_work_delayable *work, void (*handler)(struct k_work *));
int      k_work_reschedule(struct k_work_delayable *work, uint32_t delay);
void     k_timer_start(struct k_timer *timer, int initial, int period);
void     k_timer_stop(struct k_timer *timer);
