/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zmk/rgb_underglow.h>
#include "../../firmware/zmk/lighting/rgb_settings.h"

extern int (*rgb_test_init)(void);
extern int (*rgb_test_load)(const char *, size_t, settings_read_cb, void *);
extern const struct zmk_listener zmk_listener_rgb_underglow;
const struct device              rgb_test_device = {0};
uint32_t                         rgb_test_time;
static enum zmk_activity_state   activity = ZMK_ACTIVITY_ACTIVE;
static struct k_work_delayable  *native_save;
static struct k_work            *runtime_save;
static uint8_t                   saved[10];
static unsigned                  writes, native_offs;
static unsigned                  user_on_calls, user_off_calls, user_toggle_calls;
int                              __wrap_zmk_rgb_underglow_on(void);
int                              __wrap_zmk_rgb_underglow_off(void);
int                              __wrap_zmk_rgb_underglow_toggle(void);
/* GCC's test-only instrumentation ABI supplies code addresses. No firmware
 * tracing or payload capture is present in the application adapter. */
void __cyg_profile_func_enter(void *function, void *caller) {
    (void)caller;
    if (function == (void *)__wrap_zmk_rgb_underglow_on) ++user_on_calls;
    if (function == (void *)__wrap_zmk_rgb_underglow_off) ++user_off_calls;
    if (function == (void *)__wrap_zmk_rgb_underglow_toggle) ++user_toggle_calls;
}
void __cyg_profile_func_exit(void *function, void *caller) {
    (void)function;
    (void)caller;
}

uint32_t k_uptime_get_32(void) {
    return rgb_test_time;
}
int k_work_submit(struct k_work *work) {
    runtime_save  = work;
    work->pending = true;
    return 1;
}
int k_work_submit_to_queue(void *queue, struct k_work *work) {
    (void)queue;
    (void)work;
    return 1;
}
void *zmk_workqueue_lowprio_work_q(void) {
    return NULL;
}
void k_work_init_delayable(struct k_work_delayable *work, void (*handler)(struct k_work *)) {
    work->work.handler = handler;
    native_save        = work;
}
int k_work_reschedule(struct k_work_delayable *work, uint32_t delay) {
    work->due          = rgb_test_time + delay;
    work->work.pending = true;
    return 1;
}
void k_timer_start(struct k_timer *timer, int initial, int period) {
    (void)timer;
    (void)initial;
    (void)period;
}
void k_timer_stop(struct k_timer *timer) {
    (void)timer;
    ++native_offs;
}
int led_strip_update_rgb(const struct device *device, struct led_rgb *pixels, size_t count) {
    (void)device;
    (void)pixels;
    (void)count;
    return 0;
}
enum zmk_activity_state zmk_activity_get_state(void) {
    return activity;
}
int zmk_event_manager_raise(zmk_event_t *event) {
    return zmk_listener_rgb_underglow.callback(event);
}
int settings_name_steq(const char *name, const char *key, const char **next) {
    size_t length = strlen(key);
    if (strncmp(name, key, length) || name[length]) return 0;
    *next = NULL;
    return 1;
}
int settings_save_one(const char *name, const void *value, size_t size) {
    (void)name;
    (void)value;
    (void)size;
    assert(!"Native RGB save bypassed its shared runtime adapter");
    return -EINVAL;
}
int gmk_zmk_settings_save_runtime(const char *name, const void *value, size_t size) {
    assert(!strcmp(name, "rgb/underglow/state") && size == sizeof(saved));
    memcpy(saved, value, sizeof(saved));
    ++writes;
    return 0;
}
static void send_activity(enum zmk_activity_state state) {
    activity = state;
    assert(!raise_zmk_activity_state_changed((struct zmk_activity_state_changed){.state = state}));
}
static bool on(void) {
    bool state = false;
    assert(!zmk_rgb_underglow_get_state(&state));
    return state;
}
static void run_work(void) {
    if (native_save->work.pending && rgb_test_time >= native_save->due) {
        native_save->work.pending = false;
        native_save->work.handler(&native_save->work);
    }
    gmk_rgb_settings_poll(true);
    if (runtime_save && runtime_save->pending) {
        runtime_save->pending = false;
        runtime_save->handler(runtime_save);
    }
}
static ssize_t read_saved(void *context, void *destination, size_t size) {
    (void)context;
    assert(size == sizeof(saved));
    memcpy(destination, saved, sizeof(saved));
    return (ssize_t)size;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    bool start_off   = !strcmp(argv[1], "off") || !strcmp(argv[1], "idle-on");
    bool idle_off    = !strcmp(argv[1], "idle-off");
    bool idle_on     = !strcmp(argv[1], "idle-on");
    bool idle_toggle = !strcmp(argv[1], "idle-toggle");
    assert(!rgb_test_init() && on());
    if (start_off) assert(!zmk_rgb_underglow_off() && !on() && user_off_calls == 1);
    assert(!zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 130, .s = 80, .b = 30}));
    assert(gmk_rgb_settings_dirty());
    rgb_test_time       = 30000;
    unsigned off_before = user_off_calls, native_before = native_offs;
    send_activity(!strcmp(argv[1], "sleep") ? ZMK_ACTIVITY_SLEEP : ZMK_ACTIVITY_IDLE);
    assert(!on() && native_offs == native_before + 1 && user_off_calls == off_before);
    assert(gmk_rgb_settings_dirty());  /* Native's deferred snapshot must block sleep too. */
    send_activity(ZMK_ACTIVITY_SLEEP); /* Repeated inactive events preserve the same user intent. */
    if (idle_off) assert(!zmk_rgb_underglow_off() && user_off_calls == off_before + 1);
    if (idle_on) assert(!zmk_rgb_underglow_on() && user_on_calls == 1);
    if (idle_toggle) assert(!zmk_rgb_underglow_toggle() && user_toggle_calls == 1 && !on());
    bool wanted   = idle_on || (!start_off && !idle_off && !idle_toggle);
    rgb_test_time = 60000;
    run_work();
    assert(writes == 0 && gmk_rgb_settings_dirty());
    rgb_test_time = 90000;
    run_work();
    assert(writes == 1 && saved[8] == (wanted ? 1 : 0));
    assert(!gmk_rgb_settings_dirty());
    assert(saved[0] == 130 && saved[1] == 0 && saved[2] == 80 && saved[3] == 30);
    assert(saved[4] == 1 && saved[5] == 0 && saved[6] == 0 && saved[7] == 0 && saved[9] == 0);
    if (!strcmp(argv[1], "late-off")) {
        assert(!zmk_rgb_underglow_off() && gmk_rgb_settings_dirty());
        wanted = false;
        rgb_test_time += 60000;
        run_work();
        assert(saved[8] == 0 && !gmk_rgb_settings_dirty());
    }
    unsigned on_before = user_on_calls, toggle_before = user_toggle_calls;
    off_before = user_off_calls;
    send_activity(ZMK_ACTIVITY_ACTIVE);
    assert(on() == wanted);
    /* The native auto on/off calls and adapter reconciliation use real native
     * functions. Only the explicit user APIs enter the instrumented wrappers. */
    assert(user_on_calls == on_before && user_off_calls == off_before && user_toggle_calls == toggle_before);
    rgb_test_time += 60000;
    run_work();
    assert(saved[8] == (wanted ? 1 : 0));
    assert(!rgb_test_init());
    assert(!rgb_test_load("state", sizeof(saved), read_saved, NULL));
    assert(on() == wanted);
    puts("RGB idle native: temporary off, explicit intent, native call trace, debounce gate and exact ten-byte reload passed");
    return 0;
}
