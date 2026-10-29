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
bool       gmk_zmk_watchdog_init(void) {
    ++early_calls;
    return true;
}
void gmk_zmk_watchdog_feed(void) {
    ++feeds;
}
bool gmk_wired_is_left(void) {
    return true;
}
bool gmk_wired_mode(void) {
    return true;
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
    return true;
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
#include "service.c"
int main(void) {
    assert(!gates().writes_ready);
    assert(service_init() == 0 && wire_handler);
    if (!setjmp(leave_worker)) worker(NULL, NULL, NULL);
    assert(steps == 1 && raw_polls == 1 && observed.uptime_ms == 1234 && observed.matrix_events == 0);
    assert(feeds == 0 && early_calls == 0 && observed.writes_ready);
    uint8_t packet[32] = {0};
    wire_handler(packet, packet);
    assert(observed.left && observed.wired);
    raw_init_result = -1;
    assert(service_init() == -1 && !gates().writes_ready);
    unsigned previous = raw_polls;
    worker(NULL, NULL, NULL);
    assert(raw_polls == previous && feeds == 0 && early_calls == 0);
    gmk_zmk_update_reset();
    assert(reset_calls == 1 && handoff_calls == 0);
    puts("ZMK service readiness and permanent no-watchdog tests passed");
}
