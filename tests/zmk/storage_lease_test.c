/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../firmware/zmk/runtime/service.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>

static unsigned count;
#ifdef CONFIG_GMK70_ZMK_UPDATER
static bool upload_busy, held;
bool        gmk_zmk_receiver_storage_claim(void) {
    if (upload_busy || held) return false;
    held = true;
    return true;
}
void gmk_zmk_receiver_storage_release(void) {
    held = false;
}
bool gmk_zmk_receiver_idle(void) {
    return !held && !upload_busy;
}
#else
static void *client(void *unused) {
    (void)unused;
    for (unsigned i = 0; i < 2000; ++i) {
        while (!gmk_zmk_storage_claim())
            sched_yield();
        assert(!gmk_zmk_service_idle());
        assert(!gmk_zmk_storage_claim());
        unsigned previous = count;
        sched_yield();
        count = previous + 1;
        gmk_zmk_storage_release();
    }
    return NULL;
}
#endif
int main(void) {
    assert(gmk_zmk_service_idle());
    assert(gmk_zmk_storage_claim());
    assert(!gmk_zmk_service_idle() && !gmk_zmk_storage_claim());
    gmk_zmk_storage_release();
    assert(gmk_zmk_service_idle());
#ifdef CONFIG_GMK70_ZMK_UPDATER
    upload_busy = true;
    assert(!gmk_zmk_service_idle() && !gmk_zmk_storage_claim());
    upload_busy = false;
    assert(gmk_zmk_storage_claim());
    gmk_zmk_storage_release();
    (void)count;
#else
    pthread_t clients[4];
    for (unsigned i = 0; i < 4; ++i)
        assert(!pthread_create(&clients[i], NULL, client, NULL));
    for (unsigned i = 0; i < 4; ++i)
        assert(!pthread_join(clients[i], NULL));
    assert(count == 8000 && gmk_zmk_service_idle());
#endif
    puts("Shared storage lease: settings/power exclusion and updater delegation passed");
}
