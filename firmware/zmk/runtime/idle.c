/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "idle.h"
#include <zephyr/kernel.h>
/* Count-one semaphores retain IRQs arriving between the predicate and wait.
 * Never reset them before waiting: that would lose the first waking key. */
K_SEM_DEFINE(scan_ready, 0, 1);
K_SEM_DEFINE(service_ready, 0, 1);
void gmk_scan_notify(void) {
    k_sem_give(&scan_ready);
}
void gmk_service_notify(void) {
    k_sem_give(&service_ready);
}
void gmk_scan_wait(uint32_t maximum_ms) {
    (void)k_sem_take(&scan_ready, maximum_ms == UINT32_MAX ? K_FOREVER : K_MSEC(maximum_ms));
}
void gmk_service_wait(uint32_t maximum_ms) {
    (void)k_sem_take(&service_ready, maximum_ms == UINT32_MAX ? K_FOREVER : K_MSEC(maximum_ms));
}
