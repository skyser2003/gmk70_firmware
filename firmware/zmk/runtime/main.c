/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <zephyr/kernel.h>
#include <kthread.h>

int            __real_main(void);
_Noreturn void gmk_scan_run(void);

/* Reuse the initialization thread after pinned ZMK main has loaded settings.
 * The scanner keeps its original priority and 1536-byte stack; no active stack
 * is overlaid or resized. The service worker remains separate for storage. */
int __wrap_main(void) {
    int result = __real_main();
    if (result) return result;
    /* Match the native bg_thread_main tail before taking over its stack. */
    z_thread_essential_clear(k_current_get());
    k_thread_priority_set(k_current_get(), 5);
    gmk_scan_run();
}
