/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "settings_backend.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <zephyr/settings/settings.h>
#include <zmk/studio/core.h>
static struct settings_store   *backend;
static atomic_bool              busy, stop, fail_write;
static atomic_uint              writes;
static pthread_t                worker_id;
static unsigned                 loaded;
static bool                     invalid_mode;
int                             settings_backend_init(void);
enum zmk_studio_core_lock_state __wrap_zmk_studio_core_get_lock_state(void);
enum zmk_studio_core_lock_state __real_zmk_studio_core_get_lock_state(void) {
    return ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED;
}
void settings_src_register(struct settings_store *s) {
    backend = s;
}
void settings_dst_register(struct settings_store *s) {
    assert(backend == s);
}
bool gmk_zmk_receiver_storage_claim(void) {
    return !atomic_exchange(&busy, true);
}
void gmk_zmk_receiver_storage_release(void) {
    assert(atomic_exchange(&busy, false));
}
bool gmk_zmk_settings_flash_page(uint32_t address, const uint8_t data[256]) {
    assert(pthread_equal(pthread_self(), worker_id));
    assert(atomic_load(&busy));
    assert(address >= 0x0801c000 && address <= 0x0801ff00 && address % 256 == 0);
    atomic_fetch_add(&writes, 1);
    if (atomic_load(&fail_write)) return false;
    memcpy((void *)(uintptr_t)address, data, 256);
    return true;
}
int settings_call_set_handler(const char *name, size_t size, settings_read_cb read, void *ctx, const struct settings_load_arg *arg) {
    (void)arg;
    char data[16] = {0};
    assert(!strcmp(name, "keymap/test") && size == 5);
    assert(read(ctx, data, sizeof(data)) == 5 && !memcmp(data, "value", 5));
    ++loaded;
    return 0;
}
static void *worker(void *arg) {
    (void)arg;
    worker_id                = pthread_self();
    gmk_zmk_update_env_t env = {.left = true, .wired = !invalid_mode, .watchdog_ready = true};
    while (!atomic_load(&stop)) {
        gmk_zmk_settings_poll(&env);
        usleep(100);
    }
    return NULL;
}
int main(void) {
    void *flash = mmap((void *)0x0801c000, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(flash == (void *)0x0801c000);
    memset(flash, 0xff, 16384);
    assert(__wrap_zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_LOCKED);
    assert(settings_backend_init() == 0 && gmk_zmk_settings_ready());
    assert(__wrap_zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, worker, NULL));
    /* No configured USB: real startup behavior-ID saves must complete. */
    assert(backend->cs_itf->csi_save(backend, "keymap/test", "value", 5) == 0);
    assert(atomic_load(&writes) == 2 && !atomic_load(&busy));
    assert(!backend->cs_itf->csi_load(backend, NULL) && loaded == 1);
    atomic_store(&busy, true);
    unsigned before = atomic_load(&writes);
    assert(backend->cs_itf->csi_save(backend, "keymap/test", "other", 5) == -EBUSY);
    assert(atomic_load(&writes) == before);
    atomic_store(&busy, false);
    assert(!backend->cs_itf->csi_save(backend, "keymap/test", "value", 5));
    assert(atomic_load(&writes) == before);
    atomic_store(&fail_write, true);
    assert(backend->cs_itf->csi_save(backend, "keymap/test", "other", 5) == -EIO);
    assert(!gmk_zmk_settings_ready() && !atomic_load(&busy));
    assert(__wrap_zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_LOCKED);
    assert(backend->cs_itf->csi_save(backend, "keymap/test", "other", 5) == -EIO);
    atomic_store(&stop, true);
    pthread_join(thread, NULL);
    atomic_store(&fail_write, false);
    assert(settings_backend_init() == 0 && gmk_zmk_settings_ready());
    assert(!backend->cs_itf->csi_load(backend, NULL) && loaded == 2);
    memset(flash, 0x5a, 16384);
    assert(settings_backend_init() == 0 && !gmk_zmk_settings_ready());
    assert(backend->cs_itf->csi_load(backend, NULL) == -EIO);
    assert(!munmap(flash, 16384));
    puts("Studio backend: startup queue, worker ownership, update exclusion, reload and fault lock passed");
}
