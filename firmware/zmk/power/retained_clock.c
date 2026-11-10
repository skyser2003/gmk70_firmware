/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * WB32FQ95 RM2905025 §§7/17, DS1104041 table5.2-8; vendor RTCv1 driver.
 * Register facts and provenance: docs/ZMK-EVENT-POWER.md.
 */
#include "retained.h"
#include "retained_clock.h"
#include "retained_math.h"
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define RTC 0x40015800U
#define RCC 0x40010c00U
#define PWR 0x40010000U
#define BDCR 0x40015d00U
#define ANCTL 0x40010400U
#define ALARM BIT(17)
#define CYC_PER_TICK (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / CONFIG_SYS_CLOCK_TICKS_PER_SEC)
BUILD_ASSERT(CONFIG_CORTEX_M_SYSTICK_64BIT_CYCLE_COUNTER);
BUILD_ASSERT(CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC % CONFIG_SYS_CLOCK_TICKS_PER_SEC == 0);

uint32_t        __real_sys_clock_cycle_get_32(void);
uint64_t        __real_sys_clock_cycle_get_64(void);
static uint64_t cycle_offset, slept_cycles;
static uint32_t fraction, calibration_rtc, calibration_cpu, ratio_rtc, ratio_cpu, previous_rtc, saved_ctrl, poll_at, alarm_ticks;
enum { UNINITIALIZED, CALIBRATING, AVAILABLE, UNAVAILABLE };
static uint8_t state;

uint64_t __wrap_sys_clock_cycle_get_64(void) {
    unsigned key   = irq_lock();
    uint64_t value = __real_sys_clock_cycle_get_64() + cycle_offset;
    irq_unlock(key);
    return value;
}
uint32_t __wrap_sys_clock_cycle_get_32(void) {
    return (uint32_t)__wrap_sys_clock_cycle_get_64();
}
static bool wait_bits(uintptr_t address, uint32_t mask, uint32_t value) {
    /* Neither uptime nor a kernel timeout advances while STOP is owned. */
    for (unsigned attempt = 0; attempt < 100000; ++attempt)
        if ((sys_read32(address) & mask) == value) return true;
    return false;
}
void gmk_sleep_clock_gate(void) {
    sys_set_bits(RCC + 0x44, BIT(2)); /* BKP AHB2 clock, vendor rccEnableBKP. */
    sys_write32(1, RCC + 0xd8);
}
static void alarm_clear(void) {
    sys_write32(sys_read32(RTC + 4) & ~2U, RTC + 4);
    sys_write32(ALARM, 0x40001814U);
    sys_write32(BIT(2), 0xe000e284U); /* Only owned IRQ34. */
}
static void rtc_alarm_isr(const void *unused) {
    ARG_UNUSED(unused);
    alarm_clear(); /* Housekeeping never raises native activity or peer traffic. */
}
static bool sync_counter(void) {
    /* Flags are stale until resync. Write ones to W0C event flags so a
     * read/modify/write cannot erase an alarm that arrived during STOP. */
    sys_write32(7U, RTC + 4);
    return wait_bits(RTC + 4, 8, 8);
}
static bool counter(uint32_t *value) {
    /* Use documented halfwords, not the ambiguous CMSIS CNT alias. A low-half
     * rollover between bus reads must not create a backwards timestamp. */
    for (unsigned attempt = 0; attempt < 100000; ++attempt) {
        uint32_t high = sys_read32(RTC + 0x18) & 0xffffU;
        uint32_t low  = sys_read32(RTC + 0x1c) & 0xffffU;
        if (high == (sys_read32(RTC + 0x18) & 0xffffU)) {
            *value = high << 16 | low;
            return true;
        }
    }
    return false;
}
static bool configure(bool alarm) {
    uint32_t now;
    if (alarm && !counter(&now)) return false;
    if (!wait_bits(RTC + 4, 32, 32)) return false;
    sys_write32(sys_read32(RTC + 4) | 16U, RTC + 4);
    if (alarm) {
        uint32_t deadline = now + alarm_ticks;
        sys_write32(deadline >> 16, RTC + 0x20);
        sys_write32(deadline & 0xffffU, RTC + 0x24);
    } else {
        sys_write32(0, RTC + 8);
        sys_write32(0, RTC + 12); /* Free-running raw LSI; do not reset CNT/BKP. */
    }
    sys_write32(sys_read32(RTC + 4) & ~16U, RTC + 4);
    return wait_bits(RTC + 4, 32, 32);
}
/* Preserve complete returning-path verification in the linked image. */
__attribute__((noipa)) uint32_t gmk_retained_poll(void) {
    if (state == UNINITIALIZED) {
        state = UNAVAILABLE;
        if (sys_read32(0x40016418U) & BIT(1)) return UINT32_MAX; /* Read-only RST_STOP. */
        sys_set_bits(RCC + 0x44, BIT(2));                        /* Access BKP before touching any RTC clock. */
        uint32_t domain = sys_read32(BDCR);
        /* PRL is write-only; compatibility cannot be inferred by reading it.
         * These two backup words reserve ownership/version, never keys or
         * settings. Claim them only in an unconfigured, blank backup domain. */
        uint32_t mark0  = sys_read32(0x40015c5cU);
        uint32_t mark1  = sys_read32(0x40015c60U);
        bool     owned  = mark0 == 0x474d4b37 && mark1 == 0x52544301;
        uint32_t source = domain & (3U << 8);
        if ((!owned && (source || mark0 || mark1)) || (owned && source && source != (2U << 8)) || (sys_read32(RTC) & 7U)) return UINT32_MAX;
        if (sys_read32(0x40001800U) & ALARM) return UINT32_MAX;
        sys_set_bits(PWR, 1);
        sys_write32(3, PWR + 0x28);
        sys_write32(12, PWR + 0x2c);
        sys_set_bits(ANCTL + 0x44, 1);
        sys_write32(0, PWR + 0x28);
        sys_write32(0, PWR + 0x2c);
        if (!wait_bits(ANCTL + 0x48, 1, 1)) return UINT32_MAX;
        sys_write32(0x474d4b37, 0x40015c5cU);
        sys_write32(0x52544301, 0x40015c60U);
        gmk_sleep_clock_gate();
        sys_write32((domain & ~(3U << 8)) | (2U << 8) | BIT(15), BDCR);
        if (!sync_counter() || !configure(false)) return UINT32_MAX;
        IRQ_CONNECT(34, 3, rtc_alarm_isr, NULL, 0);
        state = CALIBRATING;
        if (!counter(&calibration_rtc)) {
            state = UNAVAILABLE;
            return UINT32_MAX;
        }
        calibration_cpu = (uint32_t)__real_sys_clock_cycle_get_64();
        poll_at         = k_uptime_get_32();
    }
    if (state == CALIBRATING) {
        uint32_t age = k_uptime_get_32() - poll_at;
        if (age < 64) return 64 - age;
        uint32_t cpu = (uint32_t)__real_sys_clock_cycle_get_64();
        uint32_t rtc;
        if (!counter(&rtc)) {
            state = UNAVAILABLE;
            return UINT32_MAX;
        }
        if (age > 1000) {
            calibration_cpu = cpu;
            calibration_rtc = rtc;
            poll_at         = k_uptime_get_32();
            return 64;
        }
        ratio_cpu   = cpu - calibration_cpu;
        ratio_rtc   = rtc - calibration_rtc;
        uint32_t hz = ratio_cpu ? (uint32_t)gmk_sleep_divide((uint64_t)ratio_rtc * CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC, ratio_cpu) : 0;
        state       = hz >= 19000 && hz <= 41000 ? AVAILABLE : UNAVAILABLE;
        /* The alarm only bounds counter wrap; exact elapsed time still uses
         * the unrounded ratio. Compute this coarse hourly deadline once. */
        alarm_ticks = hz * 3600U;
    }
    return UINT32_MAX;
}
bool gmk_retained_available(void) {
    return state == AVAILABLE;
}
bool gmk_sleep_clock_begin(void) {
    /* Sole caller has admitted availability and holds scheduler/hard IRQs. */
    if (!configure(true)) {
        state = UNAVAILABLE;
        return false;
    }
    alarm_clear();
    sys_write32(2, RTC); /* Alarm only; seconds/overflow do not wake the CPU. */
    sys_set_bits(0x40001808U, ALARM);
    sys_clear_bits(0x4000180cU, ALARM);
    sys_set_bits(0x40001800U, ALARM);
    irq_enable(34);
    /* The actual driver consumes COUNTFLAG and reconciles its active cycles.
     * Immediately freeze the freshly reloaded timer while hard IRQs are masked. */
    sys_clock_set_timeout(2, false);
    saved_ctrl = sys_read32(0xe000e010U);
    sys_write32(saved_ctrl & ~1U, 0xe000e010U);
    if (!counter(&previous_rtc)) {
        state = UNAVAILABLE;
        sys_write32(saved_ctrl, 0xe000e010U);
        irq_disable(34);
        sys_clear_bits(0x40001800U, ALARM);
        sys_write32(0, RTC);
        return false;
    }
    slept_cycles = 0;
    return true;
}
bool gmk_sleep_clock_checkpoint(bool *alarm) {
    /* The STOP owner has already restored our gate and the RCC snapshot. */
    if (!sync_counter()) return false;
    *alarm = (sys_read32(RTC + 4) & 2U) != 0;
    uint32_t now;
    if (!counter(&now)) return false;
    uint64_t cycles = gmk_sleep_divide((uint64_t)(uint32_t)(now - previous_rtc) * ratio_cpu, ratio_rtc);
    previous_rtc    = now;
    cycle_offset += cycles;
    slept_cycles += cycles;
    alarm_clear();
    return configure(true);
}
void gmk_sleep_clock_end(void) {
    irq_disable(34);
    sys_clear_bits(0x40001800U, ALARM);
    sys_write32(0, RTC);
    alarm_clear();
    uint64_t total = slept_cycles + fraction;
    uint64_t ticks = gmk_sleep_divide(total, CYC_PER_TICK);
    fraction       = (uint32_t)(total - ticks * CYC_PER_TICK);
    while (ticks) {
        uint32_t chunk = ticks > INT32_MAX ? INT32_MAX : (uint32_t)ticks;
        sys_clock_announce((int32_t)chunk);
        ticks -= chunk;
    }
    sys_write32(saved_ctrl, 0xe000e010U);
}
