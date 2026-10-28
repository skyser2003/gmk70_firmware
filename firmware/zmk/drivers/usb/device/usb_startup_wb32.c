/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <soc.h>

static int usb_startup_disconnect(void) {
    /* Give the host a defined D+ disconnect before USB init reconnects.
     * APPLICATION runs in thread context; do not delay under attach's IRQ lock. */
    wb32_usb_disconnect();
    k_msleep(50);
    return 0;
}

SYS_INIT(usb_startup_disconnect, APPLICATION, 89);
