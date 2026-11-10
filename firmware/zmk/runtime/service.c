/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "service.h"
#include "idle.h"
#ifdef CONFIG_GMK70_ZMK_UPDATER
#    include "../update/raw_hid.h"
#endif
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
#    include "../studio/settings_backend.h"
#    ifdef CONFIG_GMK70_WB32_RGB
#        include "../lighting/rgb_settings.h"
#    endif
#endif
#include "wired.h"
#ifdef CONFIG_GMK70_NATIVE_SPLIT
#    include "split.h"
#endif
#ifdef CONFIG_GMK70_FUNCTION_FLIP
#    include "../function_flip/flip.h"
#endif
#include "../battery/battery.h"
#ifdef CONFIG_GMK70_NKRO
#    include "../nkro/nkro.h"
#endif
#ifdef CONFIG_GMK70_RADIO
#    include "radio.h"
#endif
#ifdef CONFIG_GMK70_CONTROLS
#    include "../controls/controls.h"
#endif
#ifdef CONFIG_GMK70_POWER
#    include "../power/power.h"
#endif
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zmk/usb.h>

/* APPLICATION init publishes success before Zephyr starts static threads. */
static bool service_ready;

static gmk_zmk_service_env_t gates(void) {
    bool ready = service_ready;
#ifdef CONFIG_GMK70_POWER
    ready = ready && !gmk_power_sleep_pending();
#endif
    return (gmk_zmk_service_env_t){.left = gmk_wired_is_left(), .wired = gmk_wired_mode(), .usb_configured = zmk_usb_is_hid_ready(), .writes_ready = ready};
}

#ifdef CONFIG_GMK70_ZMK_UPDATER
static void peer_receive(const uint8_t request[32], uint8_t response[32]) {
    gmk_zmk_update_env_t env = gates();
    gmk_zmk_receiver_receive(request, response, false, &env);
}

void gmk_zmk_update_reset(void) {
    sys_reboot(SYS_REBOOT_COLD);
}
#endif

/* Worker-owned telemetry; callbacks use gates() and the receiver's cache. */
static gmk_zmk_service_env_t worker_environment(bool peer) {
    gmk_zmk_service_env_t env = gates();
    env.peer                  = peer;
    env.uptime_ms             = k_uptime_get_32();
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
#ifdef CONFIG_GMK70_NATIVE_SPLIT
        gmk_native_split_poll();
#endif
        struct gmk_wired_stats stats;
        /* Never enqueue this worker behind the responder/scanner mutex.
         * Contention means unknown peer state, represented by a cleared flag;
         * do not reuse an earlier connected indication indefinitely. */
        bool                  peer           = gmk_wired_try_get_stats(&stats) && stats.peer;
        gmk_zmk_service_env_t env            = worker_environment(peer);
        bool                  runtime_writes = env.left && env.writes_ready && env.wired && env.usb_configured;
#ifdef CONFIG_GMK70_POWER
        runtime_writes = env.left && env.writes_ready && ((env.wired && env.usb_configured) || gmk_power_runtime_storage_safe());
#endif
#ifdef CONFIG_GMK70_CONTROLS
        gmk_controls_poll(runtime_writes);
#endif
#ifdef CONFIG_GMK70_FUNCTION_FLIP
        /* These native runtime preferences have a narrow wireless allowlist.
         * Firmware and Studio writes still use the original environment. */
        gmk_flip_poll(runtime_writes);
#endif
#ifdef CONFIG_GMK70_NKRO
        gmk_nkro_poll(runtime_writes);
#endif
#if defined(CONFIG_GMK70_ZMK_STUDIO_SETTINGS) && defined(CONFIG_GMK70_WB32_RGB)
        gmk_rgb_settings_poll(runtime_writes);
#endif
        ARG_UNUSED(runtime_writes);
#ifdef CONFIG_GMK70_ZMK_UPDATER
        gmk_zmk_raw_hid_poll(&env);
        /* Admission and execution are distinct boundaries: resample physical
         * mode/USB/role after USB work, immediately before protocol dispatch. */
        env = worker_environment(peer);
        gmk_zmk_receiver_step(&env);
#endif
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
        env = worker_environment(peer);
        gmk_zmk_settings_poll(&env);
#endif
#ifdef CONFIG_GMK70_POWER
        gmk_power_poll(service_ready);
#endif
#ifdef CONFIG_GMK70_EVENT_IDLE
        /* Cable/charge input fallback and settings retry ceiling. These wakes
         * are maintenance only: they never raise native activity events. */
        uint32_t delay = gmk_wired_control_delay();
        uint32_t next  = gmk_battery_delay();
        if (next < delay) delay = next;
        next = gmk_radio_delay();
        if (next < delay) delay = next;
        next = gmk_power_delay();
        if (next < delay) delay = next;
        if (!gmk_zmk_service_idle() && delay > 1) delay = 1;
#    ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
        if (env.left && env.wired && env.usb_configured && gmk_zmk_settings_ready() && !gmk_zmk_settings_ids_ready()) delay = 1;
#    endif
#    ifdef CONFIG_GMK70_ZMK_UPDATER
        next = gmk_zmk_raw_hid_delay();
        if (next < delay) delay = next;
#    endif
        if (runtime_writes) {
            bool pending_save = false;
#    ifdef CONFIG_GMK70_CONTROLS
            pending_save |= gmk_controls_dirty();
#    endif
#    ifdef CONFIG_GMK70_FUNCTION_FLIP
            pending_save |= gmk_flip_dirty();
#    endif
#    ifdef CONFIG_GMK70_NKRO
            pending_save |= gmk_nkro_dirty();
#    endif
#    if defined(CONFIG_GMK70_ZMK_STUDIO_SETTINGS) && defined(CONFIG_GMK70_WB32_RGB)
            pending_save |= gmk_rgb_settings_dirty();
#    endif
            if (pending_save && delay > 250) delay = 250;
        }
        gmk_service_wait(delay);
#else
        k_sleep(K_MSEC(1));
#endif
    }
}
/* Static thread setup runs after APPLICATION init in the pinned Zephyr.
 * Autostart then; k_thread_start() from service_init would precede setup. */
#ifdef CONFIG_GMK70_ZMK_UPDATER
K_THREAD_DEFINE(update_worker, 2048, worker, NULL, NULL, NULL, 6, 0, 0);
#else
K_THREAD_DEFINE(service_worker, 2048, worker, NULL, NULL, NULL, 6, 0, 0);
#endif

static int service_init(void) {
    service_ready = false;
#ifdef CONFIG_GMK70_ZMK_UPDATER
    int result = gmk_zmk_raw_hid_init();
    if (result != 0) return result;
    gmk_zmk_update_env_t env = gates();
    env.uptime_ms            = k_uptime_get_32();
    gmk_zmk_receiver_init(&env);
    gmk_wired_set_update_handler(peer_receive);
#endif
    service_ready = true;
    return 0;
}
/* Updater HID registration precedes USB; ordinary services also precede main. */
SYS_INIT(service_init, APPLICATION, 90);
