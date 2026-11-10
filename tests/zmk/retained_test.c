/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <fixture.h>
#include <cmsis_core.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include "../../firmware/zmk/power/retained.h"
#include "../../firmware/zmk/power/retained_clock.h"
#include "wb32_stop.h"
#include "../../firmware/zmk/power/retained_math.h"

uint32_t test_primask, test_basepri;
unsigned test_sched_lock;
void (*test_rtc_isr)(const void *);
struct k_timer   activity_timer, battery_timer, underglow_tick;
static uint32_t  systick_remaining, rtc_hz = 32000, rtc_fraction;
static unsigned  timer_expiries, one_shots, waits, alarms, pulses, resumed, ctrl_reads;
static uint32_t  sleep_ticks = 32000;
static bool      physical_pending, clocks_ready = true, in_isr, inject_ctrl_wrap;
static uint64_t  slept_raw;
static bool      rtc_synced = true, roll_on_read, tear_case, coincidence;
static uintptr_t fault_address;
static bool      fault_write;
static struct {
    uintptr_t address;
    uint32_t  value;
} registers[160];
static unsigned register_count;
void            retained_test_systick_init(void);
void            sys_clock_isr(void);

static uint32_t *reg(uintptr_t address) {
    for (unsigned i = 0; i < register_count; ++i)
        if (registers[i].address == address) return &registers[i].value;
    assert(register_count < ARRAY_SIZE(registers));
    registers[register_count].address = address;
    return &registers[register_count++].value;
}
static void protect(int flags) {
    assert(!mprotect((void *)0xe000e000UL, 4096, flags));
}
static void advance_raw(uint32_t cycles) {
    uint64_t rtc = (uint64_t)cycles * rtc_hz + rtc_fraction;
    *reg(0x4001581cU) += (uint32_t)(rtc / 96000000U);
    rtc_fraction = (uint32_t)(rtc % 96000000U);
    if (!(SysTick->CTRL & 1U)) return;
    while (cycles >= systick_remaining) {
        cycles -= systick_remaining;
        systick_remaining = SysTick->LOAD + 1;
        SysTick->CTRL |= BIT(16);
        if (SysTick->CTRL & 2) *reg(0xe000ed04U) |= BIT(26);
    }
    systick_remaining -= cycles;
    SysTick->VAL = systick_remaining == SysTick->LOAD + 1 ? 0 : systick_remaining;
}
/* Execute the REAL pinned C driver's volatile instructions against protected
 * native MMIO. Single-step one access, then apply hardware side effects.
 * Unlike a plain struct, reading CTRL really clears COUNTFLAG. */
static void mmio_signal(int sig, siginfo_t *info, void *context) {
    ucontext_t *u = context;
    if (sig == SIGSEGV) {
        fault_address = (uintptr_t)info->si_addr;
        assert(fault_address >= 0xe000e010UL && fault_address < 0xe000e020UL);
        fault_write = (u->uc_mcontext.gregs[REG_ERR] & 2) != 0;
        protect(PROT_READ | PROT_WRITE);
        if (fault_address == 0xe000e010UL && !fault_write && inject_ctrl_wrap) {
            inject_ctrl_wrap = false;
            advance_raw(systick_remaining);
        }
        u->uc_mcontext.gregs[REG_EFL] |= 0x100;
    } else {
        assert(sig == SIGTRAP);
        if (fault_address == 0xe000e010UL && !fault_write) {
            ++ctrl_reads;
            SysTick->CTRL &= ~BIT(16);
        }
        if (fault_address == 0xe000e018UL && fault_write) {
            systick_remaining = SysTick->LOAD + 1;
            SysTick->CTRL &= ~BIT(16);
        }
        protect(PROT_NONE);
        u->uc_mcontext.gregs[REG_EFL] &= ~0x100;
    }
}
static void advance(uint32_t cycles) {
    while (cycles) {
        uint32_t step = MIN(cycles, systick_remaining);
        protect(PROT_READ | PROT_WRITE);
        advance_raw(step);
        protect(PROT_NONE);
        cycles -= step;
        if ((*reg(0xe000ed04U) & BIT(26)) && !test_primask && !test_basepri) {
            *reg(0xe000ed04U) &= ~BIT(26);
            in_isr = true;
            sys_clock_isr();
            in_isr = false;
        }
    }
}
uint32_t sys_read32(uintptr_t address) {
    if (address == 0xe000e010U) return SysTick->CTRL;
    if (address == 0x40010448U) return *reg(0x40010444U) & 1;
    if (address == 0x40015818U) {
        uint32_t high = *reg(0x4001581cU) >> 16;
        if (roll_on_read) {
            roll_on_read = false;
            ++*reg(0x4001581cU);
            ++slept_raw;
        }
        return high;
    }
    if (address == 0x4001581cU) return *reg(address) & 0xffffU;
    if (address == 0x40015808U || address == 0x4001580cU) {
        assert(false);
    }
    if (address == 0x40015804U) return (rtc_synced ? *reg(address) : *reg(address) & ~7U) | 8U | 32U;
    return *reg(address);
}
void sys_write32(uint32_t value, uintptr_t address) {
    if (address == 0xe000e010U) {
        SysTick->CTRL = value;
        return;
    }
    if (address == 0x40015804U) {
        *reg(address) = (*reg(address) & value & 7U) | (value & (8U | 16U));
        if (!(value & 8U)) rtc_synced = true;
    } else if (address == 0xe000e180U || address == 0xe000e184U) {
        *reg(address - 0x80U) &= ~value;
    } else if (address == 0xe000e100U || address == 0xe000e104U) {
        *reg(address) |= value;
    } else if (address == 0xe000e280U || address == 0xe000e284U) {
        *reg(address - 0x80U) &= ~value;
    } else if (address == 0x40001814U) {
        *reg(address) &= ~value;
    } else {
        *reg(address) = value;
    }
}
unsigned irq_lock(void) {
    unsigned key = test_basepri;
    test_basepri = 1;
    return key;
}
void irq_unlock(unsigned key) {
    test_basepri = key;
}
void irq_enable(unsigned irq) {
    sys_write32(BIT(irq % 32), 0xe000e100U + 4U * (irq / 32));
}
void irq_disable(unsigned irq) {
    sys_write32(BIT(irq % 32), 0xe000e180U + 4U * (irq / 32));
}
void k_sched_lock(void) {
    ++test_sched_lock;
}
void k_sched_unlock(void) {
    assert(test_sched_lock);
    --test_sched_lock;
}
void z_time_slice(void) {
    assert(in_isr || test_sched_lock || !test_primask);
}
static void timer_callback(struct _timeout *timeout) {
    struct k_timer *timer = CONTAINER_OF(timeout, struct k_timer, timeout);
    ++timer_expiries;
    if (timer->period.ticks) z_add_timeout(timeout, timer_callback, timer->period);
}
void k_timer_start(struct k_timer *timer, k_timeout_t duration, k_timeout_t period) {
    z_abort_timeout(&timer->timeout);
    timer->period = period;
    z_add_timeout(&timer->timeout, timer_callback, duration);
}
void k_timer_stop(struct k_timer *timer) {
    z_abort_timeout(&timer->timeout);
}
k_ticks_t k_timer_remaining_ticks(const struct k_timer *timer) {
    return z_timeout_remaining(&timer->timeout);
}
bool gmk_wired_power_wake_pending(void) {
    return physical_pending;
}
void gmk_wired_power_peer_wake(void) {
    assert(physical_pending && clocks_ready && (sys_read32(0xe000e010U) & 1U));
    ++pulses;
}
bool wb32_clock_resume(void) {
    assert(test_primask && test_sched_lock);
    clocks_ready = true;
    ++resumed;
    return true;
}
_Noreturn void sys_reboot(int type) {
    (void)type;
    assert(false);
    abort();
}
static void prepare(void) {
    assert(test_primask && test_sched_lock);
    assert(!(sys_read32(0x40010000U) & (BIT(18) | BIT(19) | (3U << 5))));
    assert(!(sys_read32(0xe000e100U) & BIT(30))); /* Quiet UART cannot wake STOP. */
}
static void waiting(void) {
    assert(test_primask && test_sched_lock);
    assert(!(sys_read32(0xe000e010U) & 1U));
    uint32_t ticks = waits < alarms ? rtc_hz * 3600U : sleep_ticks;
    if (tear_case) ticks = 0xffffU - (*reg(0x4001581cU) & 0xffffU);
    *reg(0x4001581cU) += ticks;
    slept_raw += ticks;
    if (waits++ < alarms) {
        *reg(0x40015804U) |= 2;
        *reg(0x40001814U) |= BIT(17);
        *reg(0xe000e204U) |= BIT(2);
        assert(!pulses && !one_shots);
    } else {
        physical_pending = true;
        *reg(0x40001814U) |= BIT(13);
        *reg(0xe000e204U) |= BIT(1);
    }
    clocks_ready      = false;
    *reg(0x40010c48U) = *reg(0x40010c4cU) = 0;
}
static void resumed_hook(void) {
    assert(!clocks_ready);
}
const struct wb32_stop_ops *wb32_stop_hardware_ops(void) {
    static const struct wb32_stop_ops ops = {sys_read32, sys_write32, prepare, waiting, resumed_hook};
    return &ops;
}
static void once(struct _timeout *timeout) {
    (void)timeout;
    assert(in_isr || (test_sched_lock && test_primask)); /* Catch-up or normal timer ISR. */
    ++one_shots;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    assert(mmap((void *)0xe000e000UL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)0xe000e000UL);
    struct sigaction handler = {.sa_sigaction = mmio_signal, .sa_flags = SA_SIGINFO};
    sigemptyset(&handler.sa_mask);
    assert(!sigaction(SIGSEGV, &handler, NULL) && !sigaction(SIGTRAP, &handler, NULL));
    protect(PROT_NONE);
    retained_test_systick_init();
    sys_clock_set_timeout(10, false);
    if (!strcmp(argv[1], "foreign")) *reg(0x40015d00U) = (3U << 8) | BIT(15);
    if (!strcmp(argv[1], "foreign-lsi")) *reg(0x40015d00U) = (2U << 8) | BIT(15);
    if (!strcmp(argv[1], "owned")) {
        *reg(0x40015d00U) = (2U << 8) | BIT(15);
        *reg(0x40015c5cU) = 0x474d4b37;
        *reg(0x40015c60U) = 0x52544301;
    }
    if (!strcmp(argv[1], "reset-option")) *reg(0x40016418U) = BIT(1);
    if (!strcmp(argv[1], "20k")) rtc_hz = 20000;
    if (!strcmp(argv[1], "40k")) rtc_hz = 40000;
    if (!strcmp(argv[1], "foreign-high")) *reg(0x40015c5cU) = 0xbeef0000;
    /* Independent host division is the oracle for the size-bounded M3 helper. */
    uint64_t seed = UINT64_C(0x379213a5e014bb17);
    for (unsigned sample = 0; sample < 20000; ++sample) {
        seed               = seed * UINT64_C(6364136223846793005) + 1;
        uint64_t numerator = seed;
        seed               = seed * UINT64_C(6364136223846793005) + 1;
        uint32_t divisor   = (uint32_t)(seed >> 32);
        if (!divisor) divisor = 1;
        assert(gmk_sleep_divide(numerator, divisor) == numerator / divisor);
    }
    uint32_t delay = gmk_retained_poll();
    if (!strcmp(argv[1], "foreign") || !strcmp(argv[1], "foreign-lsi") || !strcmp(argv[1], "foreign-high") || !strcmp(argv[1], "reset-option")) {
        assert(delay == UINT32_MAX && !gmk_retained_available());
        assert(!test_rtc_isr && !*reg(0x40010cd8U));
        puts("retained clock: incompatible backup/reset option refused without reset");
        return 0;
    }
    assert(delay == 64 && !gmk_retained_available());
    advance(64 * 96000U);
    assert(gmk_retained_poll() == UINT32_MAX && gmk_retained_available());
    /* Existing enabled sources and pending DISABLED IRQs must survive STOP. */
    uint32_t enabled = BIT(6) | BIT(16) | BIT(30);
    sys_write32(enabled, 0xe000e100U);
    sys_write32(BIT(1), 0xe000e104U);
    *reg(0xe000e200U) |= BIT(20);
    *reg(0x40010c48U) = 0x9abc;
    *reg(0x40010c4cU) = 0x1234;
    k_timer_start(&activity_timer, K_TICKS(1000), K_TICKS(1000));
    k_timer_start(&battery_timer, K_TICKS(3000), K_TICKS(3000));
    k_timer_start(&underglow_tick, K_TICKS(50), K_TICKS(50));
    struct _timeout pending = {0};
    z_add_timeout(&pending, once, K_TICKS(2000));
    advance(96000U / 3); /* Native, unannounced active fraction before sleep. */
    uint64_t cycles_before = sys_clock_cycle_get_64();
    int64_t  uptime_before = sys_clock_tick_get();
    if (!strcmp(argv[1], "hours")) alarms = 3;
    if (!strcmp(argv[1], "long")) alarms = 600; /* >INT_MAX kernel ticks. */
    if (!strcmp(argv[1], "wrap")) *reg(0x4001581cU) = UINT32_MAX - 15;
    tear_case   = !strcmp(argv[1], "read-rollover");
    coincidence = !strcmp(argv[1], "coincidence");
    if (!strcmp(argv[1], "kernel-pending")) {
        *reg(0xe000ed04U) |= BIT(26);
        assert(!gmk_retained_sleep() && !waits && !pulses && gmk_retained_available());
        assert(!test_primask && !test_sched_lock && (*reg(0xe000ed04U) & BIT(26)));
        *reg(0xe000ed04U) &= ~BIT(26);
    }
    unsigned repetitions = !strcmp(argv[1], "fraction") ? 200 : 1;
    sleep_ticks          = repetitions > 1 ? 1 : rtc_hz;
    for (unsigned i = 0; i < repetitions; ++i) {
        physical_pending = false;
        assert(gmk_retained_sleep());
        assert(!test_sched_lock && !test_primask && !test_basepri);
        assert(*reg(0xe000e100U) == enabled && *reg(0xe000e104U) == BIT(1));
        assert(*reg(0xe000e200U) & BIT(20));
        assert(*reg(0x40001814U) & BIT(13)); /* Physical edge never acknowledged here. */
        assert(*reg(0x40010c48U) == 0x9abc && *reg(0x40010c4cU) == 0x1234);
        /* Model the normal GPIO ISR consuming the preserved physical edge. */
        *reg(0x40001814U) &= ~BIT(13);
        *reg(0xe000e204U) &= ~BIT(1);
    }
    uint64_t expected_cycles = slept_raw * 96000000ULL / rtc_hz;
    uint64_t elapsed_cycles  = sys_clock_cycle_get_64() - cycles_before;
    assert(elapsed_cycles == expected_cycles);
    uint64_t elapsed_ticks = (uint64_t)(sys_clock_tick_get() - uptime_before);
    assert(elapsed_ticks == expected_cycles / 96000U);
    assert(timer_expiries == 0 && pulses == repetitions && resumed == waits);
    assert(one_shots == (expected_cycles >= 2000ULL * 96000U ? 1U : 0U));
    advance(20 * 96000U);
    assert(sys_clock_cycle_get_64() - cycles_before == expected_cycles + 20U * 96000U);
    assert((uint64_t)(sys_clock_tick_get() - uptime_before) == elapsed_ticks + 20);
    /* COUNTFLAG is consumed by real volatile reads; wrap between the first
     * VAL read and CTRL read is accounted once by the pinned driver. */
    inject_ctrl_wrap     = true;
    uint64_t before_wrap = sys_clock_cycle_get_64();
    assert(!inject_ctrl_wrap && sys_clock_cycle_get_64() == before_wrap);
    assert(ctrl_reads > 10);
    puts("retained STOP: actual SysTick/timeout core, calibrated sleep, hourly silence, SRAM mode, pending IRQs, clocks, fractions and active resume passed");
}
