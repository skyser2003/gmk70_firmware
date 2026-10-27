/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "raw_hid.h"
#include "wb32_watchdog.h"
#include "wired.h"
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zmk/usb.h>

/* Written before any threads/callbacks run, immutable thereafter. */
static bool     watchdog_ready;
static uint32_t feed_calls;

static int watchdog_init(void) {
    watchdog_ready = gmk_zmk_watchdog_init();
#ifdef CONFIG_GMK70_STAGING_QUALIFICATION
    watchdog_ready = false;
#endif
    /* Keep keyboard/read-only identity alive on failure; mutations are gated. */
    return 0;
}
/* SoC clock hook and C storage setup precede PRE_KERNEL_1. */
SYS_INIT(watchdog_init, PRE_KERNEL_1, 50);

static gmk_zmk_update_env_t gates(void) {
    return (gmk_zmk_update_env_t){.left = gmk_wired_is_left(), .wired = gmk_wired_mode(), .usb_configured = zmk_usb_is_hid_ready(), .watchdog_ready = watchdog_ready};
}

static void peer_receive(const uint8_t request[32], uint8_t response[32]) {
    gmk_zmk_update_env_t env = gates();
    gmk_zmk_receiver_receive(request, response, false, &env);
}

void gmk_zmk_update_reset(void) {
    sys_reboot(SYS_REBOOT_COLD);
}

static void worker(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    for (;;) {
        gmk_zmk_update_env_t   env = gates();
        struct gmk_wired_stats stats;
        gmk_wired_get_stats(&stats);
        env.peer       = stats.peer;
        env.uptime_ms  = k_uptime_get_32();
        env.feed_calls = feed_calls;
        /* No key-event counter is exposed by the scanner yet. Zero means
         * unavailable, not a transport transaction or polling count. */
        env.matrix_events = 0;
        gmk_zmk_raw_hid_poll(&env);
        gmk_zmk_receiver_step(&env);
#ifndef CONFIG_GMK70_STAGING_QUALIFICATION
        /* Feed only after this live worker completed its current flash/relay
         * work. A stalled controller or worker cannot refresh the watchdog. */
        if (watchdog_ready) {
            gmk_zmk_watchdog_feed();
            ++feed_calls;
        }
#endif
        k_sleep(K_MSEC(1));
    }
}
K_THREAD_DEFINE(update_worker, 2048, worker, NULL, NULL, NULL, 6, 0, SYS_FOREVER_MS);

static int service_init(void) {
    int result = gmk_zmk_raw_hid_init();
    if (result != 0) return result;
    gmk_zmk_update_env_t env = gates();
    env.uptime_ms            = k_uptime_get_32();
    gmk_zmk_receiver_init(&env);
    gmk_wired_set_update_handler(peer_receive);
    k_thread_start(update_worker);
    return 0;
}
/* HID1 must be registered before keyboard HID0 (95) enables USB (96). */
SYS_INIT(service_init, APPLICATION, 90);
