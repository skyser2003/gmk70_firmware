/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include "../../firmware/zmk/runtime/idle.c"
#include "../../firmware/zmk/runtime/main.c"
static int             last_timeout;
static bool            inject;
static unsigned        native_calls;
static int             native_result;
static jmp_buf         scanner_jump;
static struct k_thread main_thread;
void                   k_sem_give(struct k_sem *sem) {
    if (sem->count < sem->maximum) ++sem->count;
}
int k_sem_take(struct k_sem *sem, int timeout) {
    last_timeout = timeout;
    if (inject) {
        inject = false;
        k_sem_give(sem);
    }
    if (!sem->count) return -EAGAIN;
    --sem->count;
    return 0;
}
struct k_thread *k_current_get(void) {
    return &main_thread;
}
void z_thread_essential_clear(struct k_thread *thread) {
    assert(native_calls == 1);
    thread->essential = 0;
}
void k_thread_priority_set(struct k_thread *thread, int priority) {
    assert(native_calls == 1 && !thread->essential);
    thread->priority = priority;
}
int __real_main(void) {
    ++native_calls;
    return native_result;
}
_Noreturn void gmk_scan_run(void) {
    assert(native_calls == 1 && main_thread.priority == 5);
    longjmp(scanner_jump, 1);
}
int main(void) {
    gmk_scan_notify(); /* IRQ before entering the wait is retained. */
    gmk_scan_wait(UINT32_MAX);
    assert(!scan_ready.count && !service_ready.count && last_timeout == K_FOREVER);
    inject = true; /* IRQ between deadline computation and semaphore take. */
    gmk_scan_wait(500);
    assert(!inject && !scan_ready.count && last_timeout == 500);
    for (unsigned n = 0; n < 10000; ++n)
        gmk_service_notify();
    assert(service_ready.count == 1 && !scan_ready.count);
    gmk_service_wait(1000);
    assert(!service_ready.count && last_timeout == 1000);
    main_thread.essential = 1;
    native_result         = -EIO;
    assert(__wrap_main() == -EIO && native_calls == 1);
    native_calls  = 0;
    native_result = 0;
    if (!setjmp(scanner_jump)) {
        (void)__wrap_main();
        assert(false);
    }
    assert(native_calls == 1 && main_thread.priority == 5);
    puts("idle scheduler: IRQ/wait races, bounded wake coalescing, native initialization and scanner priority passed");
}
