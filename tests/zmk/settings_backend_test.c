/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "settings_backend.h"
#include "settings_store.h"
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
#include "settings_name_test.h"
#ifdef CONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE
#    include <drivers/behavior.h>
static struct device             settings_test_devices[2];
struct zmk_behavior_local_id_map settings_test_ids[2];
#endif
static struct settings_store *backend;
static atomic_bool            busy, stop, fail_write;
static atomic_bool            usb_ready, pause_worker, worker_paused;
static atomic_bool            wireless_mode, runtime_safe, missing_role, writes_unready;
static atomic_uint            writes;
static pthread_t              worker_id;
static unsigned               loaded;
static bool                   invalid_mode, invalid_role;
static bool                   side_left = true;
static unsigned               claims;
static bool                   migration_enabled;
static unsigned               migrated, following_loaded, preferred_loaded;
extern pthread_mutex_t        settings_access_mutex __asm__("access");
static atomic_int             native_lock_state = ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED;
static atomic_uint            lock_notifications, unlock_notifications;
static atomic_int             notified_lock_state;
static bool                   check_notification_locks;
bool                          settings_test_in_isr;
int                           __wrap_zmk_settings_erase(void);
bool                          gmk_power_runtime_storage_safe(void) {
    return atomic_load(&runtime_safe);
}
bool gmk_wired_is_left(void) {
    return side_left;
}
int settings_name_steq(const char *name, const char *key, const char **next) {
    return gmk_test_settings_name_steq(name, key, next);
}
int                             settings_backend_init(void);
enum zmk_studio_core_lock_state __wrap_zmk_studio_core_get_lock_state(void);
enum zmk_studio_core_lock_state __real_zmk_studio_core_get_lock_state(void) {
    return (enum zmk_studio_core_lock_state)atomic_load(&native_lock_state);
}
static void *probe_notification_mutex(void *context) {
    int *result = context;
    *result     = pthread_mutex_trylock(&settings_access_mutex);
    if (!*result) assert(!pthread_mutex_unlock(&settings_access_mutex));
    return NULL;
}
int raise_zmk_studio_core_lock_state_changed(struct zmk_studio_core_lock_state_changed event) {
    if (check_notification_locks) {
        assert(!atomic_load(&busy));
        pthread_t thread;
        int       result = -1;
        assert(!pthread_create(&thread, NULL, probe_notification_mutex, &result));
        assert(!pthread_join(thread, NULL) && !result);
    }
    atomic_store(&notified_lock_state, event.state);
    atomic_fetch_add(&lock_notifications, 1);
    if (event.state == ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED) atomic_fetch_add(&unlock_notifications, 1);
    return 0;
}
void settings_src_register(struct settings_store *s) {
    backend = s;
}
void settings_dst_register(struct settings_store *s) {
    assert(backend == s);
}
bool gmk_zmk_storage_claim(void) {
    ++claims;
    return !atomic_exchange(&busy, true);
}
void gmk_zmk_storage_release(void) {
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
    assert(size <= sizeof(data) && read(ctx, data, sizeof(data)) == (ssize_t)size);
    if (!strcmp(name, "keymap/test")) {
        assert(size == 5 && !memcmp(data, "value", 5));
        ++loaded;
    }
    if (!strcmp(name, "endpoints/preferred") && migration_enabled) {
        migration_enabled = false;
        ++migrated;
        /* Pinned native endpoints h_set performs these two synchronous
         * writes while the settings backend is still walking its records. */
        assert(!backend->cs_itf->csi_save(backend, "endpoints/preferred", NULL, 0));
        assert(!backend->cs_itf->csi_save(backend, "endpoints/preferred2", "1", 1));
    }
    if (!strcmp(name, "migration/after")) ++following_loaded;
    if (!strcmp(name, "endpoints/preferred2")) ++preferred_loaded;
    return 0;
}
static void *worker(void *arg) {
    (void)arg;
    worker_id                 = pthread_self();
    gmk_zmk_service_env_t env = {.left = !invalid_role, .wired = !invalid_mode, .writes_ready = true};
    while (!atomic_load(&stop)) {
        if (atomic_load(&pause_worker)) {
            atomic_store(&worker_paused, true);
            usleep(100);
            continue;
        }
        atomic_store(&worker_paused, false);
        env.left           = !invalid_role && !atomic_load(&missing_role);
        env.wired          = !invalid_mode && !atomic_load(&wireless_mode);
        env.writes_ready   = !atomic_load(&writes_unready);
        env.usb_configured = atomic_load(&usb_ready);
        gmk_zmk_settings_poll(&env);
        usleep(100);
    }
    return NULL;
}
static void wait_for(atomic_bool *value) {
    for (unsigned n = 0; n < 10000 && !atomic_load(value); ++n)
        usleep(100);
    assert(atomic_load(value));
}
static void *runtime_save(void *arg) {
    int *result = arg;
    *result     = gmk_zmk_settings_save_runtime("keymap/test", "other", 5);
    return NULL;
}
#ifdef CONFIG_GMK70_POWER
static void *runtime_preference_save(void *arg) {
    int *result = arg;
    *result     = gmk_zmk_settings_save_runtime("gmk_controls/state", "prefs", 5);
    return NULL;
}
#endif
static void wireless_preferences(void) {
    atomic_store(&wireless_mode, true);
    atomic_store(&runtime_safe, true);
    atomic_store(&usb_ready, false);
    const char *const names[] = {"rgb/underglow/state", "gmk_nkro/state", "gmk_flip/state", "gmk_controls/state"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        unsigned before = atomic_load(&writes);
        int      result = gmk_zmk_settings_save_runtime(names[i], "prefs", 5);
#ifdef CONFIG_GMK70_POWER
        assert(result == 0 && atomic_load(&writes) > before);
#else
        assert(result == -ENOTSUP && atomic_load(&writes) == before);
#endif
    }
    unsigned before = atomic_load(&writes);
    assert(gmk_zmk_settings_save_runtime("keymap/test", "bad", 3) == -ENOTSUP);
    assert(gmk_zmk_settings_save_runtime("gmk_flip/state/extra", "bad", 3) == -ENOTSUP);
    assert(gmk_zmk_settings_save_runtime("gmk_controls/stateX", "bad", 3) == -ENOTSUP);
    assert(backend->cs_itf->csi_save(backend, "gmk_flip/state", "bad", 3) == -ENOTSUP);
    assert(atomic_load(&writes) == before);
#ifdef CONFIG_GMK70_POWER
    /* Recheck each safety input after the submitting feature has queued work. */
    for (unsigned gate = 0; gate < 3; ++gate) {
        atomic_store(&pause_worker, true);
        wait_for(&worker_paused);
        pthread_t thread;
        int       result = 0;
        assert(!pthread_create(&thread, NULL, runtime_preference_save, &result));
        wait_for(&busy);
        if (gate == 0) atomic_store(&runtime_safe, false);
        if (gate == 1) atomic_store(&missing_role, true);
        if (gate == 2) atomic_store(&writes_unready, true);
        atomic_store(&pause_worker, false);
        pthread_join(thread, NULL);
        assert(result == -ENOTSUP && atomic_load(&writes) == before);
        atomic_store(&runtime_safe, true);
        atomic_store(&missing_role, false);
        atomic_store(&writes_unready, false);
    }
#endif
    atomic_store(&wireless_mode, false);
    unsigned before_wired = atomic_load(&writes);
    int      result       = gmk_zmk_settings_save_runtime("gmk_controls/state", "wired", 5);
#ifdef CONFIG_GMK70_POWER
    assert(result == 0 && atomic_load(&writes) > before_wired);
#else
    assert(result == -ENOTSUP && atomic_load(&writes) == before_wired);
#endif
    assert(gmk_zmk_settings_save_runtime("keymap/test", "bad", 3) == -ENOTSUP);
    atomic_store(&usb_ready, true);
}
static bool read_only_probe(void *context, uint32_t offset, const uint8_t page[GMK_SETTINGS_PAGE]) {
    (void)context;
    (void)offset;
    (void)page;
    assert(!"mount must not write");
    return false;
}
static void migration_during_load(void) {
    assert(!backend->cs_itf->csi_save(backend, "endpoints/preferred", "0", 1));
    assert(!backend->cs_itf->csi_save(backend, "migration/after", "after", 5));
    /* Make deletion compact the inactive bank, invalidating the old cursor's
     * snapshot/journal positions before h_set appends the upgraded value. */
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        gmk_settings_store_t probe;
        assert(!gmk_settings_store_mount(&probe, (const uint8_t *)0x0801c000, read_only_probe, NULL));
        if (probe.next_page == GMK_SETTINGS_BANK / GMK_SETTINGS_PAGE) break;
        assert(attempt != 63);
        assert(!backend->cs_itf->csi_save(backend, "migration/filler", (const char *)&attempt, sizeof(attempt)));
    }
    migration_enabled = true;
    assert(!backend->cs_itf->csi_load(backend, NULL));
    assert(migrated == 1 && following_loaded > 0 && preferred_loaded > 0);
    assert(gmk_zmk_settings_ready());
    assert(!gmk_zmk_settings_factory_reset());
    assert(!backend->cs_itf->csi_save(backend, "keymap/test", "value", 5));
}
static void factory_reset_gates(void) {
    unsigned before = atomic_load(&writes);
    atomic_store(&wireless_mode, true);
    atomic_store(&runtime_safe, true);
    assert(gmk_zmk_settings_factory_reset() == -ENOTSUP);
    atomic_store(&wireless_mode, false);
    atomic_store(&usb_ready, false);
    assert(__wrap_zmk_settings_erase() == -ENOTSUP);
    atomic_store(&usb_ready, true);
    atomic_store(&missing_role, true);
    assert(gmk_zmk_settings_factory_reset() == -ENOTSUP);
    atomic_store(&missing_role, false);
    atomic_store(&writes_unready, true);
    assert(gmk_zmk_settings_factory_reset() == -ENOTSUP);
    atomic_store(&writes_unready, false);
    side_left              = false;
    unsigned before_claims = claims;
    assert(gmk_zmk_settings_factory_reset() == -ENODEV && claims == before_claims);
    side_left            = true;
    settings_test_in_isr = true;
    assert(gmk_zmk_settings_factory_reset() == -EWOULDBLOCK);
    settings_test_in_isr = false;
    atomic_store(&busy, true);
    assert(gmk_zmk_settings_factory_reset() == -EBUSY);
    atomic_store(&busy, false);
    assert(atomic_load(&writes) == before);
    assert(__wrap_zmk_settings_erase() == 0);
    assert(atomic_load(&writes) == before + 1 && gmk_zmk_settings_ready());
    unsigned loaded_before = loaded;
    assert(backend->cs_itf->csi_load(backend, NULL) == 0 && loaded == loaded_before);
    assert(backend->cs_itf->csi_save(backend, "keymap/test", "value", 5) == 0);
}
#ifdef CONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE
static void native_ids_wireless_startup(void) {
    settings_test_devices[0] = (struct device){.name = "kp", .ready = true};
    settings_test_devices[1] = (struct device){.name = "rgb", .ready = true};
    settings_test_ids[0]     = (struct zmk_behavior_local_id_map){.device = &settings_test_devices[0]};
    settings_test_ids[1]     = (struct zmk_behavior_local_id_map){.device = &settings_test_devices[1]};
    assert(!settings_backend_init() && !gmk_zmk_settings_ids_ready());
    assert(__wrap_zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_LOCKED);
    atomic_store(&wireless_mode, true);
    atomic_store(&runtime_safe, true);
    atomic_store(&usb_ready, false);
    atomic_store(&stop, false);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, worker, NULL));
    settings_test_ids[0].local_id = 7;
    settings_test_ids[1].local_id = 8;
    assert(backend->cs_itf->csi_save(backend, "behavior/local_id/7", "kp", 2) == -ENOTSUP);
    assert(backend->cs_itf->csi_save(backend, "behavior/local_id/8", "rgb", 3) == -ENOTSUP);
    assert(!gmk_zmk_settings_ids_ready());
    assert(!gmk_zmk_settings_save_runtime("gmk_controls/state", "prefs", 5));
    atomic_store(&stop, true);
    pthread_join(thread, NULL);
    worker_id                = pthread_self();
    check_notification_locks = true;
    atomic_store(&lock_notifications, 0);
    atomic_store(&unlock_notifications, 0);
    unsigned              before = atomic_load(&writes);
    gmk_zmk_service_env_t env    = {.left = true, .wired = true, .usb_configured = true, .writes_ready = true};
    atomic_store(&busy, true);
    gmk_zmk_settings_poll(&env);
    assert(atomic_load(&writes) == before && !gmk_zmk_settings_ids_ready());
    atomic_store(&busy, false);
    env.usb_configured = false;
    gmk_zmk_settings_poll(&env);
    env.usb_configured = true;
    env.wired          = false;
    gmk_zmk_settings_poll(&env);
    env.wired = true;
    env.left  = false;
    gmk_zmk_settings_poll(&env);
    env.left         = true;
    env.writes_ready = false;
    gmk_zmk_settings_poll(&env);
    env.writes_ready = true;
    assert(atomic_load(&writes) == before && !gmk_zmk_settings_ids_ready());
    gmk_zmk_settings_poll(&env);
    assert(atomic_load(&writes) == before + 1 && !gmk_zmk_settings_ids_ready());
    gmk_zmk_settings_poll(&env);
    assert(atomic_load(&writes) == before + 2 && !gmk_zmk_settings_ids_ready());
    gmk_zmk_settings_poll(&env);
    assert(atomic_load(&writes) == before + 2 && gmk_zmk_settings_ids_ready());
    assert(atomic_load(&lock_notifications) == 1 && atomic_load(&unlock_notifications) == 1);
    assert(atomic_load(&notified_lock_state) == ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED);
    gmk_zmk_settings_poll(&env);
    gmk_zmk_settings_poll(&env);
    assert(atomic_load(&lock_notifications) == 1);
    assert(__wrap_zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED);
    gmk_settings_store_t probe;
    assert(!gmk_settings_store_mount(&probe, (const uint8_t *)0x0801c000, read_only_probe, NULL));
    const uint8_t *value;
    size_t         size;
    assert(!gmk_settings_store_get(&probe, "behavior/local_id/7", &value, &size) && size == 2 && !memcmp(value, "kp", 2));
    assert(!gmk_settings_store_get(&probe, "behavior/local_id/8", &value, &size) && size == 3 && !memcmp(value, "rgb", 3));
    assert(!settings_backend_init() && !gmk_zmk_settings_ids_ready());
    atomic_store(&native_lock_state, ZMK_STUDIO_CORE_LOCK_STATE_LOCKED);
    gmk_zmk_settings_poll(&env);
    assert(gmk_zmk_settings_ids_ready() && atomic_load(&writes) == before + 2);
    assert(atomic_load(&lock_notifications) == 2 && atomic_load(&unlock_notifications) == 1);
    assert(atomic_load(&notified_lock_state) == ZMK_STUDIO_CORE_LOCK_STATE_LOCKED);
    assert(__wrap_zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_LOCKED);
    gmk_zmk_settings_poll(&env);
    assert(atomic_load(&lock_notifications) == 2);
}
#endif
int main(void) {
    /* Zephyr mutexes permit same-owner recursion, including h_set migrations
     * that call settings_save_one/delete while the backend holds access. */
    pthread_mutexattr_t attributes;
    assert(!pthread_mutexattr_init(&attributes));
    assert(!pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE));
    assert(!pthread_mutex_destroy(&settings_access_mutex));
    assert(!pthread_mutex_init(&settings_access_mutex, &attributes));
    assert(!pthread_mutexattr_destroy(&attributes));
    void *flash = mmap((void *)0x0801c000, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(flash == (void *)0x0801c000);
    memset(flash, 0xff, 16384);
    /* A peripheral must not acquire or initialize central storage. Native
     * behavior-ID commits can still run, but their saves fail immediately. */
    side_left = false;
    assert(!settings_backend_init() && !gmk_zmk_settings_ready());
    assert(__wrap_zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_LOCKED);
    assert(backend->cs_itf->csi_load(backend, NULL) == -EIO);
    for (unsigned i = 0; i < 20; ++i)
        assert(backend->cs_itf->csi_save(backend, "behavior/local_id/1", "kp", 2) == -EIO);
    assert(gmk_zmk_settings_save_runtime("gmk_flip/state", "1", 1) == -EIO);
    assert(!claims && !atomic_load(&busy) && !atomic_load(&writes));
    assert(!mprotect(flash, 16384, PROT_NONE));
    assert(!settings_backend_init() && !gmk_zmk_settings_ready());
    assert(!mprotect(flash, 16384, PROT_READ | PROT_WRITE));
    side_left = true;
    assert(__wrap_zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_LOCKED);
    assert(settings_backend_init() == 0 && gmk_zmk_settings_ready());
#ifdef CONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE
    assert(__wrap_zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_LOCKED);
#else
    assert(__wrap_zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED);
#endif
    pthread_t thread;
    /* Removing the watchdog never removes physical role/wired gates. */
    for (unsigned gate = 0; gate < 2; ++gate) {
        invalid_mode = gate == 0;
        invalid_role = gate == 1;
        assert(!pthread_create(&thread, NULL, worker, NULL));
        assert(backend->cs_itf->csi_save(backend, "keymap/test", "value", 5) == -ENOTSUP);
        assert(!atomic_load(&writes) && gmk_zmk_settings_ready());
        atomic_store(&stop, true);
        pthread_join(thread, NULL);
        atomic_store(&stop, false);
    }
    invalid_mode = invalid_role = false;
    assert(!pthread_create(&thread, NULL, worker, NULL));
    /* No configured USB: real startup behavior-ID saves must complete. */
    assert(backend->cs_itf->csi_save(backend, "keymap/test", "value", 5) == 0);
    assert(atomic_load(&writes) == 2 && !atomic_load(&busy));
    assert(!backend->cs_itf->csi_load(backend, NULL) && loaded == 1);
    /* USB can disappear after the feature schedules its save. The real
     * storage dispatch must reject it, preserving pre-enumeration startup. */
    unsigned before_runtime = atomic_load(&writes);
    atomic_store(&pause_worker, true);
    wait_for(&worker_paused);
    atomic_store(&usb_ready, true);
    pthread_t runtime_thread;
    int       runtime_result = 0;
    assert(!pthread_create(&runtime_thread, NULL, runtime_save, &runtime_result));
    wait_for(&busy);
    atomic_store(&usb_ready, false);
    atomic_store(&pause_worker, false);
    pthread_join(runtime_thread, NULL);
    assert(runtime_result == -ENOTSUP && atomic_load(&writes) == before_runtime);
    atomic_store(&usb_ready, true);
    assert(gmk_zmk_settings_save_runtime("keymap/test", "other", 5) == 0);
    assert(atomic_load(&writes) > before_runtime);
    assert(backend->cs_itf->csi_save(backend, "keymap/test", "value", 5) == 0);
    wireless_preferences();
    factory_reset_gates();
    migration_during_load();
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
    unsigned loaded_before_reload = loaded;
    assert(settings_backend_init() == 0 && gmk_zmk_settings_ready());
    assert(!backend->cs_itf->csi_load(backend, NULL) && loaded == loaded_before_reload + 1);
    memset(flash, 0x5a, 16384);
    assert(settings_backend_init() == 0 && !gmk_zmk_settings_ready());
    assert(backend->cs_itf->csi_load(backend, NULL) == -EIO);
    /* Explicit native reset recovers a corrupt application bank; ordinary
     * mounting above remains read-only and never reformats unknown data. */
    atomic_store(&stop, false);
    assert(!pthread_create(&thread, NULL, worker, NULL));
    assert(gmk_zmk_settings_factory_reset() == 0 && gmk_zmk_settings_ready());
    atomic_store(&stop, true);
    pthread_join(thread, NULL);
#ifdef CONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE
    native_ids_wireless_startup();
#endif
    assert(!munmap(flash, 16384));
    puts("Studio backend: startup queue, worker ownership, update exclusion, reload and fault lock passed");
}
