/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <zephyr/sys/dlist.h>
#define ARG_UNUSED(v) (void)(v)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BUILD_ASSERT(...) _Static_assert(__VA_ARGS__, "retained fixture invariant")
#define BIT(n) (UINT32_C(1) << (n))
#define IS_ENABLED(v) (v)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, l, h) MIN(MAX(v, l), h)
#define DIV_ROUND_UP(v, d) (((v) + (d) - 1) / (d))
#define CONTAINER_OF(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define __ASSERT(v, ...) assert(v)
#define __ASSERT_NO_MSG(v) assert(v)
#define CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC 96000000
#define CONFIG_SYS_CLOCK_TICKS_PER_SEC 1000
#define CONFIG_CORTEX_M_SYSTICK_64BIT_CYCLE_COUNTER 1
#define CONFIG_TICKLESS_KERNEL 1
#define CONFIG_SMP 0
#define CONFIG_TIMEOUT_64BIT 0
#define CONFIG_SYSTEM_CLOCK_SLOPPY_IDLE 0
#define CONFIG_TIMESLICING 1
#define CONFIG_SYSTEM_CLOCK_INIT_PRIORITY 0
#define CONFIG_GMK70_RETAINED_SLEEP 1
#define CONFIG_ZMK_BATTERY_REPORTING 1
#define CONFIG_ZMK_RGB_UNDERGLOW 1
#define SYS_INIT(...)
#define PRE_KERNEL_2 0
#define ARCH_ISR_DIAG_OFF
#define ARCH_ISR_DIAG_ON
#define ISR_DIRECT_PM() ((void)0)
#define _IRQ_PRIO_OFFSET 0
#define SysTick_IRQn (-1)
static inline void NVIC_SetPriority(int irq, int priority) {
    (void)irq;
    (void)priority;
}
static inline void     z_arm_int_exit(void) {}
static inline uint32_t sys_clock_hw_cycles_per_sec(void) {
    return CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;
}
typedef uint32_t k_ticks_t;
typedef struct {
    k_ticks_t ticks;
} k_timeout_t;
typedef struct {
    uint64_t tick;
} k_timepoint_t;
#define K_TICKS_FOREVER UINT32_MAX
#define K_FOREVER ((k_timeout_t){UINT32_MAX})
#define K_NO_WAIT ((k_timeout_t){0})
#define K_TICKS(t) ((k_timeout_t){(k_ticks_t)(t)})
#define K_TIMEOUT_EQ(a, b) ((a).ticks == (b).ticks)
#define Z_TICK_ABS(t) (-1)
struct _timeout {
    sys_dnode_t node;
    void (*fn)(struct _timeout *);
    int32_t dticks;
};
typedef void (*_timeout_func_t)(struct _timeout *);
static inline bool z_is_inactive_timeout(const struct _timeout *t) {
    return !sys_dnode_is_linked(&t->node);
}
extern uint32_t test_primask, test_basepri;
extern unsigned test_sched_lock;
unsigned        irq_lock(void);
void            irq_unlock(unsigned key);
void            irq_enable(unsigned irq);
void            irq_disable(unsigned irq);
extern void (*test_rtc_isr)(const void *);
#define IRQ_CONNECT(irq, priority, handler, arg, flags) (test_rtc_isr = (handler))
struct k_spinlock {
    bool held;
};
typedef unsigned               k_spinlock_key_t;
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held);
    lock->held = true;
    return irq_lock();
}
static inline void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    assert(lock->held);
    lock->held = false;
    irq_unlock(key);
}
#define K_SPINLOCK(lock) for (unsigned _key = k_spin_lock(lock), _once = 1; _once; k_spin_unlock(lock, _key), _once = 0)
static inline uint32_t __get_PRIMASK(void) {
    return test_primask;
}
static inline uint32_t __get_BASEPRI(void) {
    return test_basepri;
}
static inline void __disable_irq(void) {
    test_primask = 1;
}
static inline void __set_PRIMASK(uint32_t v) {
    test_primask = v;
}
static inline void __set_BASEPRI(uint32_t v) {
    test_basepri = v;
}
static inline void __DSB(void) {}
static inline void __ISB(void) {}
void               k_sched_lock(void);
void               k_sched_unlock(void);
uint32_t           sys_read32(uintptr_t addr);
void               sys_write32(uint32_t value, uintptr_t addr);
static inline void sys_set_bits(uintptr_t addr, uint32_t bits) {
    sys_write32(sys_read32(addr) | bits, addr);
}
static inline void sys_clear_bits(uintptr_t addr, uint32_t bits) {
    sys_write32(sys_read32(addr) & ~bits, addr);
}
struct k_timer {
    struct _timeout timeout;
    k_timeout_t     period;
    uint32_t        status;
};
void                   k_timer_stop(struct k_timer *timer);
void                   k_timer_start(struct k_timer *timer, k_timeout_t duration, k_timeout_t period);
k_ticks_t              k_timer_remaining_ticks(const struct k_timer *timer);
void                   z_add_timeout(struct _timeout *to, _timeout_func_t fn, k_timeout_t timeout);
int                    z_abort_timeout(struct _timeout *to);
k_ticks_t              z_timeout_remaining(const struct _timeout *to);
int32_t                z_get_next_timeout_expiry(void);
void                   sys_clock_set_timeout(int32_t ticks, bool idle);
uint32_t               sys_clock_elapsed(void);
uint32_t               sys_clock_cycle_get_32(void);
uint64_t               sys_clock_cycle_get_64(void);
void                   sys_clock_announce(int32_t ticks);
int64_t                sys_clock_tick_get(void);
uint32_t               sys_clock_tick_get_32(void);
void                   z_time_slice(void);
static inline uint32_t k_uptime_get_32(void) {
    return sys_clock_tick_get_32();
}
static inline int64_t k_uptime_get(void) {
    return sys_clock_tick_get();
}
#define SYS_REBOOT_WARM 1
_Noreturn void sys_reboot(int type);
