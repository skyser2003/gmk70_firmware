/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "receiver.h"
#include "wired.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
static jmp_buf  leave_worker;
static unsigned feeds, steps, reset_calls, early_calls, raw_polls;
static unsigned handoff_calls;
bool            gmk_zmk_resident_handoff(void) {
    ++handoff_calls;
    return false;
}
static gmk_zmk_update_env_t observed;
static void (*wire_handler)(const uint8_t[32], uint8_t[32]);
static int raw_init_result;
#ifdef CONFIG_GMK70_POWER
static bool     test_left = true, test_wired = true, test_usb = true, test_sleep, test_storage = true;
static unsigned runtime_polls, power_polls;
static bool     runtime_allowed;
bool            gmk_power_sleep_pending(void) {
    return test_sleep;
}
bool gmk_power_runtime_storage_safe(void) {
    return test_storage;
}
void gmk_power_poll(bool ready) {
    assert(ready);
    ++power_polls;
}
void gmk_controls_poll(bool allowed) {
    runtime_allowed = allowed;
    ++runtime_polls;
}
void gmk_flip_poll(bool allowed) {
    assert(allowed == runtime_allowed);
    ++runtime_polls;
}
void gmk_nkro_poll(bool allowed) {
    assert(allowed == runtime_allowed);
    ++runtime_polls;
}
void gmk_rgb_settings_poll(bool allowed) {
    assert(allowed == runtime_allowed);
    ++runtime_polls;
}
void gmk_zmk_settings_poll(const gmk_zmk_update_env_t *env) {
    assert(env->writes_ready == !test_sleep);
#    ifndef CONFIG_GMK70_ZMK_UPDATER
    observed = *env;
#    endif
}
#endif
bool gmk_zmk_watchdog_init(void) {
    ++early_calls;
    return true;
}
void gmk_zmk_watchdog_feed(void) {
    ++feeds;
}
bool gmk_wired_is_left(void) {
#ifdef CONFIG_GMK70_POWER
    return test_left;
#else
    return true;
#endif
}
bool gmk_wired_mode(void) {
#ifdef CONFIG_GMK70_POWER
    return test_wired;
#else
    return true;
#endif
}
void gmk_wired_set_update_handler(void (*handler)(const uint8_t[32], uint8_t[32])) {
    wire_handler = handler;
}
void gmk_wired_get_stats(struct gmk_wired_stats *stats) {
    *stats = (struct gmk_wired_stats){.peer = true};
}
bool gmk_wired_try_get_stats(struct gmk_wired_stats *stats) {
    gmk_wired_get_stats(stats);
    return true;
}
bool zmk_usb_is_hid_ready(void) {
#ifdef CONFIG_GMK70_POWER
    return test_usb;
#else
    return true;
#endif
}
int gmk_zmk_raw_hid_init(void) {
    return raw_init_result;
}
void gmk_zmk_raw_hid_poll(const gmk_zmk_update_env_t *env) {
    ++raw_polls;
    observed = *env;
}
void gmk_zmk_receiver_init(const gmk_zmk_update_env_t *env) {
    observed = *env;
}
void gmk_zmk_receiver_receive(const uint8_t in[32], uint8_t out[32], bool usb, const gmk_zmk_update_env_t *env) {
    (void)in;
    (void)out;
    assert(!usb);
    observed = *env;
}
void gmk_zmk_receiver_step(const gmk_zmk_update_env_t *env) {
    ++steps;
    observed = *env;
}
void k_sleep(int ms) {
    assert(ms > 0);
    longjmp(leave_worker, 1);
}
uint32_t k_uptime_get_32(void) {
    return 1234;
}
void sys_reboot(int type) {
    assert(type == 0);
    ++reset_calls;
}
#include "../../firmware/zmk/runtime/service.c"
int main(void) {
    assert(!gates().writes_ready);
    assert(service_init() == 0);
#ifdef CONFIG_GMK70_ZMK_UPDATER
    assert(wire_handler);
#else
    assert(!wire_handler);
#endif
    if (!setjmp(leave_worker)) worker(NULL, NULL, NULL);
    assert(observed.uptime_ms == 1234 && observed.matrix_events == 0);
#ifdef CONFIG_GMK70_ZMK_UPDATER
    assert(steps == 1 && raw_polls == 1);
#else
    assert(steps == 0 && raw_polls == 0);
#endif
    assert(feeds == 0 && early_calls == 0 && observed.writes_ready);

#ifdef CONFIG_GMK70_ZMK_UPDATER
    uint8_t packet[32] = {0};
    wire_handler(packet, packet);
    assert(observed.left && observed.wired);
#endif
#ifdef CONFIG_GMK70_POWER
    assert(runtime_polls == 4 && runtime_allowed && power_polls == 1);
    test_wired = test_usb = false;
    if (!setjmp(leave_worker)) worker(NULL, NULL, NULL);
    assert(runtime_allowed && observed.writes_ready && !observed.wired && !observed.usb_configured);
    test_storage = false;
    if (!setjmp(leave_worker)) worker(NULL, NULL, NULL);
    assert(!runtime_allowed);
    test_storage = true;
    test_sleep   = true;
    if (!setjmp(leave_worker)) worker(NULL, NULL, NULL);
    assert(!runtime_allowed && !observed.writes_ready);
    test_sleep = false;
    test_left  = false;
    if (!setjmp(leave_worker)) worker(NULL, NULL, NULL);
    assert(!runtime_allowed);
#endif

#ifdef CONFIG_GMK70_ZMK_UPDATER
    raw_init_result = -1;
    assert(service_init() == -1 && !gates().writes_ready);
    unsigned previous = raw_polls;
    worker(NULL, NULL, NULL);
    assert(raw_polls == previous && feeds == 0 && early_calls == 0);
    gmk_zmk_update_reset();
    assert(reset_calls == 1 && handoff_calls == 0);
#else
    assert(!wire_handler && !raw_polls && !steps && !reset_calls && !handoff_calls);
#endif
    puts("ZMK service readiness and permanent no-watchdog tests passed");
}
