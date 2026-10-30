/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "raw_hid.h"
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
#    include "../studio/settings_backend.h"
#endif
#include "wired.h"
#ifdef CONFIG_GMK70_FUNCTION_FLIP
#    include "../function_flip/flip.h"
#endif
#include "../battery/battery.h"
#ifdef CONFIG_GMK70_RADIO
#    include "radio.h"
#endif
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zmk/usb.h>

/* APPLICATION init publishes success before Zephyr starts static threads. */
static bool service_ready;

static gmk_zmk_update_env_t gates(void) {
    return (gmk_zmk_update_env_t){.left = gmk_wired_is_left(), .wired = gmk_wired_mode(), .usb_configured = zmk_usb_is_hid_ready(), .writes_ready = service_ready};
}

static void peer_receive(const uint8_t request[32], uint8_t response[32]) {
    gmk_zmk_update_env_t env = gates();
    gmk_zmk_receiver_receive(request, response, false, &env);
}

void gmk_zmk_update_reset(void) {
    sys_reboot(SYS_REBOOT_COLD);
}

/* Worker-owned telemetry; callbacks use gates() and the receiver's cache. */
static gmk_zmk_update_env_t worker_environment(bool peer) {
    gmk_zmk_update_env_t env = gates();
    env.peer                 = peer;
    env.uptime_ms            = k_uptime_get_32();
    /* No key-event counter is exposed by the scanner yet. Zero means
     * unavailable, not a transport transaction or polling count. */
    env.matrix_events = 0;
    return env;
}

static void worker(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    if (!service_ready) return;
    for (;;) {
#ifdef CONFIG_GMK70_RADIO
        gmk_radio_poll();
#endif
        gmk_battery_poll();
        struct gmk_wired_stats stats;
        /* Never enqueue this worker behind the responder/scanner mutex.
         * Contention means unknown peer state, represented by a cleared flag;
         * do not reuse an earlier connected indication indefinitely. */
        bool                 peer = gmk_wired_try_get_stats(&stats) && stats.peer;
        gmk_zmk_update_env_t env  = worker_environment(peer);
#ifdef CONFIG_GMK70_FUNCTION_FLIP
        /* The settings caller runs on system work, never on its own storage
         * worker. Radio changes remain runtime-only until wired USB returns. */
        gmk_flip_poll(env.left && env.wired && env.usb_configured && env.writes_ready);
#endif
        gmk_zmk_raw_hid_poll(&env);
        /* Admission and execution are distinct boundaries: resample physical
         * mode/USB/role after USB work, immediately before protocol dispatch. */
        env = worker_environment(peer);
        gmk_zmk_receiver_step(&env);
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
        env = worker_environment(peer);
        gmk_zmk_settings_poll(&env);
#endif
        k_sleep(K_MSEC(1));
    }
}
/* Static thread setup runs after APPLICATION init in the pinned Zephyr.
 * Autostart then; k_thread_start() from service_init would precede setup. */
K_THREAD_DEFINE(update_worker, 2048, worker, NULL, NULL, NULL, 6, 0, 0);

static int service_init(void) {
    service_ready = false;
    int result    = gmk_zmk_raw_hid_init();
    if (result != 0) return result;
    gmk_zmk_update_env_t env = gates();
    env.uptime_ms            = k_uptime_get_32();
    gmk_zmk_receiver_init(&env);
    gmk_wired_set_update_handler(peer_receive);
    service_ready = true;
    return 0;
}
/* HID1 must be registered before keyboard HID0 (95) enables USB (96). */
SYS_INIT(service_init, APPLICATION, 90);
