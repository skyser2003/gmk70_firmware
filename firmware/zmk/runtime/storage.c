/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "service.h"
#ifdef CONFIG_GMK70_ZMK_UPDATER
#    include "../update/receiver.h"
bool gmk_zmk_storage_claim(void) {
    return gmk_zmk_receiver_storage_claim();
}
void gmk_zmk_storage_release(void) {
    gmk_zmk_receiver_storage_release();
}
bool gmk_zmk_service_idle(void) {
    return gmk_zmk_receiver_idle();
}
#else
#    include <zephyr/kernel.h>
static struct k_spinlock lock;
static bool              busy;
bool                     gmk_zmk_storage_claim(void) {
    k_spinlock_key_t key     = k_spin_lock(&lock);
    bool             claimed = !busy;
    if (claimed) busy = true;
    k_spin_unlock(&lock, key);
    return claimed;
}
void gmk_zmk_storage_release(void) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    busy                 = false;
    k_spin_unlock(&lock, key);
}
/* Retain standalone service admission in the linked image checks. */
__attribute__((noipa)) bool gmk_zmk_service_idle(void) {
    k_spinlock_key_t key  = k_spin_lock(&lock);
    bool             idle = !busy;
    k_spin_unlock(&lock, key);
    return idle;
}
#endif
