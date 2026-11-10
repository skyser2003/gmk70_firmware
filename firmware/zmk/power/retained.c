/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "retained.h"
#include "retained_clock.h"
#include "../wired/wired.h"
#include "wb32_stop.h"
#include <soc.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define RCC 0x40010c00U
#define PWR 0x40010000U
#define NVIC_ISER 0xe000e100U
#define NVIC_ICER 0xe000e180U
#define NVIC_ISPR 0xe000e200U
#define SCR 0xe000ed10U

/* Only writable clock configuration/enables. Never replay reset/status, backup
 * reset, or watchdog registers. Prescaler/divider state precedes gate restore. */
static const uint8_t  clock_offsets[] = {0x14, 0x18, 0x1c, 0x20, 0x24, 0x28, 0x2c, 0x34, 0x38, 0x3c, 0x40, 0x44, 0x48, 0x4c, 0x5c, 0x60, 0x6c, 0x70, 0x74, 0x78, 0x7c, 0xd8, 0xdc};
extern struct k_timer activity_timer;
#ifdef CONFIG_ZMK_WPM
extern struct k_timer wpm_timer;
#endif
#ifdef CONFIG_ZMK_DISPLAY
extern struct k_timer display_timer;
#endif
#ifdef CONFIG_ZMK_BATTERY_REPORTING
extern struct k_timer battery_timer;
#endif
#ifdef CONFIG_ZMK_RGB_UNDERGLOW
extern struct k_timer underglow_tick;
#endif
static struct k_timer *const periodic[] = {
    &activity_timer,
#ifdef CONFIG_ZMK_WPM
    &wpm_timer,
#endif
#ifdef CONFIG_ZMK_DISPLAY
    &display_timer,
#endif
#ifdef CONFIG_ZMK_BATTERY_REPORTING
    &battery_timer,
#endif
#ifdef CONFIG_ZMK_RGB_UNDERGLOW
    &underglow_tick,
#endif
};
static void stop_once(void) {
    const struct wb32_stop_ops *ops = wb32_stop_hardware_ops();
    uint32_t                    scr = sys_read32(SCR);
    /* Retain BOTH SRAM banks; the manual's 0x7B004 SP4 table powers one off.
     * The owner's SysTick/clock bookkeeping is separate from QMK's primitive. */
    sys_write32((sys_read32(PWR) & 9U) | 0x3b004U, PWR);
    sys_write32(0x3b3U, PWR + 0x20);
    ops->prepare();
    sys_write32(scr | 4U, SCR);
    __DSB();
    __ISB();
    ops->wait();
    ops->resume();
    sys_write32(scr, SCR);
}
/* Preserve complete returning-path verification in the linked image. */
__attribute__((noipa)) bool gmk_retained_sleep(void) {
    if (!gmk_retained_available()) return false;
    /* Pinned k_timer_stop leaves period intact. IRQ/scheduler exclusion keeps
     * these native timers' NULL stop callbacks and periods unchanged. */
    k_ticks_t timers[ARRAY_SIZE(periodic)];
    uint32_t  clocks[ARRAY_SIZE(clock_offsets)], nvic[2];
    bool      physical = false;
    /* Timeout catch-up can ready threads. Do not let a kernel callback attempt
     * a context switch while PRIMASK still masks PendSV. */
    k_sched_lock();
    uint32_t primask = __get_PRIMASK(), basepri = __get_BASEPRI();
    __disable_irq();
    __set_BASEPRI(0);
    for (unsigned i = 0; i < ARRAY_SIZE(periodic); ++i) {
        timers[i] = k_timer_remaining_ticks(periodic[i]);
        k_timer_stop(periodic[i]);
    }
    for (unsigned bank = 0; bank < 2; ++bank) {
        nvic[bank] = sys_read32(NVIC_ISER + 4U * bank);
        if (sys_read32(NVIC_ISPR + 4U * bank) & nvic[bank]) goto out;
    }
    if ((sys_read32(0xe000ed04U) & (BIT(26) | BIT(28))) || gmk_wired_power_wake_pending()) goto out;
    for (unsigned i = 0; i < ARRAY_SIZE(clock_offsets); ++i)
        clocks[i] = sys_read32(RCC + clock_offsets[i]);
    uint32_t pwr = sys_read32(PWR), cfgr = sys_read32(PWR + 0x20);
    if (!gmk_sleep_clock_begin()) goto out;
    /* Only armed GPIO wake groups and our RTC alarm may end full STOP. This
     * masks USB/UART maintenance without clearing their pending status. */
    for (unsigned bank = 0; bank < 2; ++bank)
        sys_write32(UINT32_MAX, NVIC_ICER + 4U * bank);
    sys_write32(nvic[0] & (0x7c0U | BIT(16)), NVIC_ISER);
    sys_write32((nvic[1] & BIT(1)) | BIT(2), NVIC_ISER + 4);
    for (;;) {
        if (gmk_wired_power_wake_pending()) {
            physical = true;
            break;
        }
        stop_once();
        /* RCC can be reset, but no GPIO/UART/NVIC register state is cleared. */
        gmk_sleep_clock_gate();
        if (!wb32_clock_resume()) sys_reboot(SYS_REBOOT_WARM);
        for (unsigned i = 0; i < ARRAY_SIZE(clock_offsets); ++i)
            sys_write32(clocks[i], RCC + clock_offsets[i]);
        bool alarm;
        physical = gmk_wired_power_wake_pending();
        if (!gmk_sleep_clock_checkpoint(&alarm)) sys_reboot(SYS_REBOOT_WARM);
        if (physical) break;
        /* An unrecognized source is a hardware/timebase failure, not an idle
         * event that may CANCEL an already-sleeping peer after a time jump. */
        if (!alarm) sys_reboot(SYS_REBOOT_WARM);
        /* RTC-only checkpoint: no native activity, reports, LED render, peer
         * pulse, kernel timer replay or radio/UART maintenance. */
    }
    sys_write32(pwr, PWR);
    sys_write32(cfgr, PWR + 0x20);
    sys_write32(BIT(2) | BIT(3), PWR + 4); /* Clear SPF/CKF, not GPIO/NVIC causes. */
    gmk_sleep_clock_end();
    /* k_busy_wait uses the native cycle clock: it must be running first. */
    if (physical) gmk_wired_power_peer_wake();
    for (unsigned bank = 0; bank < 2; ++bank) {
        sys_write32(UINT32_MAX, NVIC_ICER + 4U * bank);
        sys_write32(nvic[bank], NVIC_ISER + 4U * bank);
    }
out:
    physical |= gmk_wired_power_wake_pending();
    for (unsigned i = 0; i < ARRAY_SIZE(periodic); ++i)
        if (timers[i]) k_timer_start(periodic[i], K_TICKS(timers[i]), periodic[i]->period);
    __set_BASEPRI(basepri);
    __set_PRIMASK(primask);
    k_sched_unlock();
    return physical;
}
