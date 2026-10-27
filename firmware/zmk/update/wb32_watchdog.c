/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "wb32_watchdog.h"
#include <stdint.h>
#include <zephyr/sys/sys_io.h>

#ifndef CONFIG_GMK70_STAGING_QUALIFICATION
/* Westberry EN_RM2905025_WB32FQ95xx_V01: sections 5.4.9/10, 7.2.12,
 * 7.3.11/16, 11.4.6/7, 18 and 27.4.2. Same register contract as the maintained
 * trial_watchdog.c; no QMK runtime or trial reset-cause state is needed here. */
#    define KR 0x40010800U
#    define PR 0x40010804U
#    define RLR 0x40010808U
#    define SR 0x4001080cU
#    define POLL_LIMIT 1000000U
static bool ready;

static bool wait_bits(uintptr_t address, uint32_t mask, uint32_t expected) {
    /* Finite engineering bound, independent of timers and IRQ delivery. */
    for (uint32_t remaining = POLL_LIMIT; remaining; --remaining) {
        if ((sys_read32(address) & mask) == expected) return true;
    }
    return false;
}

static bool update_bits(uintptr_t address, uint32_t clear, uint32_t set) {
    sys_write32((sys_read32(address) & ~clear) | set, address);
    return (sys_read32(address) & (clear | set)) == set;
}
#endif

bool gmk_zmk_watchdog_init(void) {
#ifdef CONFIG_GMK70_STAGING_QUALIFICATION
    /* Keep the short trial watchdog as the independent rollback deadline. */
    return false;
#else
    ready = false;
    if (!update_bits(0xe0042004U, 0x100, 0) || !update_bits(0x40010c3cU, 0, 4) || !update_bits(0x40010c64U, 0, 1)) return false;
    sys_write32(3, 0x40010028U);
    sys_write32(12, 0x4001002cU);
    sys_write32(sys_read32(0x40010444U) | 1U, 0x40010444U);
    sys_write32(0, 0x40010028U);
    sys_write32(0, 0x4001002cU);
    if (!(sys_read32(0x40010444U) & 1U) || !wait_bits(0x40010448U, 1, 1) || !wait_bits(SR, 3, 0)) return false;
    sys_write32(0x5555, KR);
    sys_write32(6, PR);
    sys_write32(4095, RLR);
    if (!wait_bits(SR, 3, 0) || (sys_read32(PR) & 7U) != 6 || (sys_read32(RLR) & 0xfffU) != 4095) {
        sys_write32(0, KR);
        return false;
    }
    /* Longest hardware period: nominally /256 * 4096 / 32 kHz = 32.768 s.
     * Reload an inherited running watchdog before issuing start. All failure
     * exits precede both keys; the early trial watchdog remains unrefreshed. */
    sys_write32(0xaaaa, KR);
    sys_write32(0xcccc, KR);
    ready = true;
    return true;
#endif
}

void gmk_zmk_watchdog_feed(void) {
#ifndef CONFIG_GMK70_STAGING_QUALIFICATION
    if (ready) sys_write32(0xaaaa, KR);
#endif
}
