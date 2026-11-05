/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "settings_backend.h"
#include "settings_store.h"
#include "settings_ids.h"
#include "../wired/wired.h"
#ifdef CONFIG_GMK70_POWER
#    include "../power/power.h"
#endif
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zmk/studio/core.h>
#ifdef CONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE
#    include <drivers/behavior.h>
#    include <stdio.h>
#    include <zephyr/sys/iterable_sections.h>
#endif

#define SETTINGS_BASE 0x0801c000U
static gmk_settings_store_t store;
static atomic_t             ready;
enum { STORE_READY = 1, IDS_READY = 2 };
K_MUTEX_DEFINE(access);
K_SEM_DEFINE(completed, 0, 1);
static struct k_spinlock job_lock;
enum settings_operation { SAVE_NATIVE, SAVE_RUNTIME, FACTORY_RESET };
static struct {
    char                    name[GMK_SETTINGS_NAME_MAX + 1];
    uint8_t                 value[GMK_SETTINGS_VALUE_MAX];
    size_t                  size;
    enum settings_operation operation;
    bool                    pending, deleting;
    int                     result;
} job;

static bool write_page(void *ctx, uint32_t offset, const uint8_t page[256]) {
    ARG_UNUSED(ctx);
    if (offset % 256 || offset > GMK_SETTINGS_BYTES - 256) return false;
    return gmk_zmk_settings_flash_page(SETTINGS_BASE + offset, page);
}

bool gmk_zmk_settings_ready(void) {
    return (atomic_get(&ready) & STORE_READY) != 0;
}

bool gmk_zmk_settings_ids_ready(void) {
#ifdef CONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE
    return (atomic_get(&ready) & (STORE_READY | IDS_READY)) == (STORE_READY | IDS_READY);
#else
    return gmk_zmk_settings_ready();
#endif
}

/* Zephyr's generic settings_load does not propagate backend errors. Keep
 * secured Studio RPCs closed when storage cannot be read or written safely. */
enum zmk_studio_core_lock_state __real_zmk_studio_core_get_lock_state(void);
enum zmk_studio_core_lock_state __wrap_zmk_studio_core_get_lock_state(void) {
    return gmk_zmk_settings_ids_ready() ? __real_zmk_studio_core_get_lock_state() : ZMK_STUDIO_CORE_LOCK_STATE_LOCKED;
}

static int load(struct settings_store *cs, const struct settings_load_arg *arg) {
    ARG_UNUSED(cs);
    k_mutex_lock(&access, K_FOREVER);
    atomic_and(&ready, ~IDS_READY);
    int                   result = gmk_zmk_settings_ready() ? 0 : -EIO;
    gmk_settings_cursor_t cursor;
    gmk_settings_cursor_init(&cursor);
    unsigned index = 0, restarts = 0;
    while (!result && index < store.count) {
        uint64_t                         generation = store.generation;
        uint8_t                          next_page  = store.next_page;
        char                             name[GMK_SETTINGS_NAME_MAX + 1];
        struct gmk_settings_read_context r;
        result = gmk_settings_store_next(&store, &cursor, name, &r.value, &r.size);
#ifdef CONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE
        if (!result) result = gmk_settings_reserve_orphan_id(name, &r, arg);
#endif
        if (!result) result = settings_call_set_handler(name, r.size, gmk_settings_read_value, &r, arg);
        /* Native h_set migrations may recursively delete/save settings and
         * compact the active bank. Restart after mutation so no stale cursor
         * skips later values. Native migrations converge by removing their
         * old key; bound restarts to reject a nonconverging callback. */
        if (!result && (store.generation != generation || store.next_page != next_page)) {
            if (++restarts > GMK_SETTINGS_ENTRIES) {
                result = -ELOOP;
                break;
            }
            gmk_settings_cursor_init(&cursor);
            index = 0;
        } else
            ++index;
    }
    if (result) atomic_clear(&ready);
    k_mutex_unlock(&access);
    return result;
}

static int submit_operation(const char *name, const char *value, size_t size, enum settings_operation operation) {
    bool   reset  = operation == FACTORY_RESET;
    size_t length = 0;
    if (!reset) {
        if (!name || (!value && size)) return -EINVAL;
        length = strnlen(name, GMK_SETTINGS_NAME_MAX + 1);
        if (!length) return -EINVAL;
        if (length > GMK_SETTINGS_NAME_MAX || size > GMK_SETTINGS_VALUE_MAX) return -E2BIG;
    }
    bool native_id = operation == SAVE_NATIVE && !strncmp(name, "behavior/local_id/", sizeof("behavior/local_id/") - 1);
    if (native_id) atomic_and(&ready, ~IDS_READY);
    k_mutex_lock(&access, K_FOREVER);
    /* Confirmation may have completed while this caller waited for access. */
    if (native_id) atomic_and(&ready, ~IDS_READY);
    int result = -EIO;
    if (reset ? !store.bytes || !store.write : !gmk_zmk_settings_ready()) goto out;
    if (!gmk_zmk_receiver_storage_claim()) {
        result = -EBUSY;
        goto out;
    }
    k_spinlock_key_t key = k_spin_lock(&job_lock);
    if (reset)
        job.name[0] = '\0';
    else
        memcpy(job.name, name, length + 1);
    if (size) memcpy(job.value, value, size);
    job.size      = size;
    job.deleting  = value == NULL;
    job.operation = operation;
    job.pending   = true;
    k_spin_unlock(&job_lock, key);
    /* Startup behavior-ID writes use the same queue. The static update
     * worker starts before ZMK main and needs no configured USB to serve it.
     * It never calls settings APIs while this caller holds settings_lock. */
    k_sem_take(&completed, K_FOREVER);
    result = job.result;
out:
    k_mutex_unlock(&access);
    return result;
}

static int save(struct settings_store *cs, const char *name, const char *value, size_t size) {
    ARG_UNUSED(cs);
    return submit_operation(name, value, size, SAVE_NATIVE);
}

int gmk_zmk_settings_save_runtime(const char *name, const void *value, size_t size) {
    return submit_operation(name, value, size, SAVE_RUNTIME);
}

int gmk_zmk_settings_factory_reset(void) {
    if (k_is_in_isr()) return -EWOULDBLOCK;
    if (!gmk_wired_is_left()) return -ENODEV;
    return submit_operation(NULL, NULL, 0, FACTORY_RESET);
}

int __wrap_zmk_settings_erase(void) {
    return gmk_zmk_settings_factory_reset();
}

#ifdef CONFIG_GMK70_POWER
static bool runtime_name_allowed(const char *name) {
    return !strcmp(name, "rgb/underglow/state") || !strcmp(name, "gmk_nkro/state") || !strcmp(name, "gmk_flip/state") || !strcmp(name, "gmk_controls/state");
}
#endif

#ifdef CONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE
static void confirm_native_ids(const gmk_zmk_update_env_t *env) {
    if (gmk_zmk_settings_ids_ready() || !gmk_zmk_settings_ready() || !env->left || !env->wired || !env->usb_configured || !env->writes_ready) return;
    if (k_mutex_lock(&access, K_NO_WAIT)) return;
    bool notify = false;
    if (!gmk_zmk_settings_ready() || !gmk_zmk_receiver_storage_claim()) goto out;
    bool complete = true;
    STRUCT_SECTION_FOREACH(zmk_behavior_local_id_map, item) {
        if (!device_is_ready(item->device)) continue;
        if (!item->local_id) {
            complete = false;
            continue;
        }
        char   key[32], name[32];
        size_t length = strnlen(item->device->name, sizeof(name));
        if (length == sizeof(name)) {
            complete = false;
            break;
        }
        snprintf(key, sizeof(key), "behavior/local_id/%u", (unsigned)item->local_id);
        const uint8_t *stored;
        size_t         size;
        if (!gmk_settings_store_get(&store, key, &stored, &size) && size == length && !memcmp(stored, item->device->name, length)) continue;
        /* Keep native names and numeric IDs, including its RAM-copy boundary.
         * One missing/different entry per service poll; no extra worker/queue. */
        memcpy(name, item->device->name, length + 1);
        (void)gmk_settings_store_save(&store, key, name, length);
        complete = false;
        if (store.fault) atomic_clear(&ready);
        break;
    }
    if (complete) notify = (atomic_or(&ready, IDS_READY) & IDS_READY) == 0;
    gmk_zmk_receiver_storage_release();
out:
    k_mutex_unlock(&access);
    /* The native core may already be unlocked, so its explicit unlock call
     * would emit no event. Publish the now-visible state after all storage
     * ownership is released, once for this confirmation transition. */
    if (notify) (void)raise_zmk_studio_core_lock_state_changed((struct zmk_studio_core_lock_state_changed){.state = __real_zmk_studio_core_get_lock_state()});
}
#endif

void gmk_zmk_settings_poll(const gmk_zmk_update_env_t *env) {
    k_spinlock_key_t key     = k_spin_lock(&job_lock);
    bool             pending = job.pending;
    k_spin_unlock(&job_lock, key);
    if (!pending) {
#ifdef CONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE
        confirm_native_ids(env);
#endif
        return;
    }
    int  result  = -ENOTSUP;
    bool allowed = env->left && env->writes_ready && env->wired && (job.operation == SAVE_NATIVE || env->usb_configured);
#ifdef CONFIG_GMK70_POWER
    if (env->left && env->writes_ready && job.operation == SAVE_RUNTIME && runtime_name_allowed(job.name) && gmk_power_runtime_storage_safe()) allowed = true;
#endif
    if (allowed) {
        result = job.operation == FACTORY_RESET ? gmk_settings_store_factory_reset(&store) : gmk_settings_store_save(&store, job.name, job.deleting ? NULL : job.value, job.size);
        if (!result && job.operation == FACTORY_RESET) atomic_set(&ready, STORE_READY);
    }
    if (store.fault) atomic_clear(&ready);
    key         = k_spin_lock(&job_lock);
    job.result  = result;
    job.pending = false;
    k_spin_unlock(&job_lock, key);
    gmk_zmk_receiver_storage_release();
    k_sem_give(&completed);
}

static const struct settings_store_itf itf     = {.csi_load = load, .csi_save = save};
static struct settings_store           backend = {.cs_itf = &itf};
int                                    settings_backend_init(void) {
    /* A universal peripheral has no central settings owner. Do not even read
     * its application tail: it can contain unrelated historical firmware.
     * Native behavior-ID commits may assign RAM IDs; their saves fail below
     * before claiming storage or queueing work, so startup cannot deadlock. */
    int result = -ENODEV;
    if (gmk_wired_is_left()) result = gmk_settings_store_mount(&store, (const uint8_t *)SETTINGS_BASE, write_page, NULL);
    /* Register even on failure so generic settings calls fail explicitly.
     * Never format unknown nonblank storage or touch external Vial SPI. */
    settings_src_register(&backend);
    settings_dst_register(&backend);
    atomic_set(&ready, result == 0 ? STORE_READY : 0);
    return 0;
}
