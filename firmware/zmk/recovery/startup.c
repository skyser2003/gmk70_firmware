/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "recovery.h"
#include "../wired/wired.h"
#include <zephyr/kernel.h>

static struct gmk_recovery_startup startup;
static enum gmk_recovery_action    pending;
static bool                        dispatched;

static void dispatch(struct k_work *work) {
    (void)work;
    /* The scanner publishes pending before submitting once. Neither the UART
     * service owner nor an ISR may block on the settings storage worker. */
    (void)gmk_recovery_request(pending);
}
K_WORK_DEFINE(startup_work, dispatch);

void gmk_recovery_startup_poll(const uint16_t rows[5]) {
    if (!gmk_wired_is_left() || dispatched) return;
    bool                     middle_released = rows[1] == 0 && rows[2] == 0 && rows[3] == 0;
    bool                     bootloader      = rows[0] == 3 && middle_released && rows[4] == 0;
    bool                     settings        = rows[0] == 3 && middle_released && rows[4] == 4;
    enum gmk_recovery_action action          = gmk_recovery_startup_sample(&startup, bootloader, settings, k_uptime_get_32());
    if (action != GMK_RECOVERY_NONE) pending = action;
    if (pending != GMK_RECOVERY_NONE && rows[0] == 0 && middle_released && rows[4] == 0) {
        dispatched = true;
        (void)k_work_submit(&startup_work);
    }
}
