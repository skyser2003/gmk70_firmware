/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../firmware/zmk/recovery/startup.c"

static uint32_t                 now;
static bool                     left = true;
static unsigned                 submissions, requests;
static struct k_work           *queued;
static enum gmk_recovery_action requested;
bool                            gmk_wired_is_left(void) {
    return left;
}
uint32_t k_uptime_get_32(void) {
    return now;
}
int k_work_submit(struct k_work *work) {
    ++submissions;
    queued = work;
    return 1;
}
int __wrap_gmk_recovery_request(enum gmk_recovery_action action) {
    ++requests;
    requested = action;
    return -1; /* An authorization failure must not cause repeated retries. */
}
bool k_is_in_isr(void) {
    return false;
}
void sys_write32(uint32_t value, uintptr_t address) {
    (void)value;
    (void)address;
    abort();
}
_Noreturn void sys_reboot(int type) {
    (void)type;
    abort();
}
int zmk_settings_erase(void) {
    abort();
}
static void reset(void) {
    startup    = (struct gmk_recovery_startup){0};
    pending    = GMK_RECOVERY_NONE;
    dispatched = false;
    now = submissions = requests = 0;
    left                         = true;
    queued                       = NULL;
}
int main(void) {
    for (unsigned settings = 0; settings < 2; ++settings) {
        reset();
        uint16_t rows[5] = {3, 0, 0, 0, settings ? 4 : 0};
        gmk_recovery_startup_poll(rows);
        now = 2999;
        gmk_recovery_startup_poll(rows);
        assert(!submissions);
        now = 3000;
        gmk_recovery_startup_poll(rows);
        assert(!submissions && !requests);
        rows[0] = 0;
        gmk_recovery_startup_poll(rows);
        assert(submissions == (settings ? 0U : 1U));
        rows[4] = 0;
        gmk_recovery_startup_poll(rows);
        assert(submissions == 1 && !requests && queued);
        queued->handler(queued);
        assert(requests == 1 && requested == (settings ? GMK_RECOVERY_SETTINGS_RESET : GMK_RECOVERY_BOOTLOADER));
        gmk_recovery_startup_poll(rows);
        assert(submissions == 1);
    }
    reset();
    uint16_t rows[5] = {3, 1}; /* Extra key cancels the exact chord. */
    gmk_recovery_startup_poll(rows);
    now = 3000;
    gmk_recovery_startup_poll(rows);
    memset(rows, 0, sizeof(rows));
    gmk_recovery_startup_poll(rows);
    assert(!submissions);
    reset();
    left    = false;
    rows[0] = 3;
    gmk_recovery_startup_poll(rows);
    now = 3000;
    gmk_recovery_startup_poll(rows);
    memset(rows, 0, sizeof(rows));
    gmk_recovery_startup_poll(rows);
    assert(!submissions && !startup.sampled);
    puts("Startup recovery: exact local chords, continuous hold, release, deferred dispatch and role gate passed");
}
