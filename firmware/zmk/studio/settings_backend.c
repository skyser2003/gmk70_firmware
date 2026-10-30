/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "settings_backend.h"
#include "settings_store.h"
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zmk/studio/core.h>

#define SETTINGS_BASE 0x0801c000U
static gmk_settings_store_t store;
static atomic_t             ready;
K_MUTEX_DEFINE(access);
K_SEM_DEFINE(completed, 0, 1);
static struct k_spinlock job_lock;
static struct {
    char    name[GMK_SETTINGS_NAME_MAX + 1];
    uint8_t value[GMK_SETTINGS_VALUE_MAX];
    size_t  size;
    bool    pending, deleting, require_usb;
    int     result;
} job;

static bool write_page(void *ctx, uint32_t offset, const uint8_t page[256]) {
    ARG_UNUSED(ctx);
    if (offset % 256 || offset > GMK_SETTINGS_BYTES - 256) return false;
    return gmk_zmk_settings_flash_page(SETTINGS_BASE + offset, page);
}

bool gmk_zmk_settings_ready(void) {
    return atomic_get(&ready) != 0;
}

/* Zephyr's generic settings_load does not propagate backend errors. Keep
 * secured Studio RPCs closed when storage cannot be read or written safely. */
enum zmk_studio_core_lock_state __real_zmk_studio_core_get_lock_state(void);
enum zmk_studio_core_lock_state __wrap_zmk_studio_core_get_lock_state(void) {
    return gmk_zmk_settings_ready() ? __real_zmk_studio_core_get_lock_state() : ZMK_STUDIO_CORE_LOCK_STATE_LOCKED;
}

struct read_context {
    const uint8_t *value;
    size_t         size;
};
static ssize_t read_value(void *ctx, void *data, size_t size) {
    struct read_context *r = ctx;
    if (size > r->size) size = r->size;
    memcpy(data, r->value, size);
    return (ssize_t)size;
}

static int load(struct settings_store *cs, const struct settings_load_arg *arg) {
    ARG_UNUSED(cs);
    k_mutex_lock(&access, K_FOREVER);
    int result = gmk_zmk_settings_ready() ? 0 : -EIO;
    for (unsigned i = 0; !result && i < store.count; ++i) {
        char                name[GMK_SETTINGS_NAME_MAX + 1];
        struct read_context r;
        result = gmk_settings_store_entry(&store, i, name, &r.value, &r.size);
        if (!result) result = settings_call_set_handler(name, r.size, read_value, &r, arg);
    }
    if (result) atomic_clear(&ready);
    k_mutex_unlock(&access);
    return result;
}

static int save_value(const char *name, const char *value, size_t size, bool require_usb) {
    if (!name || (!value && size)) return -EINVAL;
    size_t length = strnlen(name, GMK_SETTINGS_NAME_MAX + 1);
    if (!length) return -EINVAL;
    if (length > GMK_SETTINGS_NAME_MAX || size > GMK_SETTINGS_VALUE_MAX) return -E2BIG;
    k_mutex_lock(&access, K_FOREVER);
    int result = -EIO;
    if (!gmk_zmk_settings_ready()) goto out;
    if (!gmk_zmk_receiver_storage_claim()) {
        result = -EBUSY;
        goto out;
    }
    k_spinlock_key_t key = k_spin_lock(&job_lock);
    memcpy(job.name, name, length + 1);
    if (size) memcpy(job.value, value, size);
    job.size        = size;
    job.deleting    = value == NULL;
    job.require_usb = require_usb;
    job.pending     = true;
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
    return save_value(name, value, size, false);
}

int gmk_zmk_settings_save_runtime(const char *name, const void *value, size_t size) {
    return save_value(name, value, size, true);
}

void gmk_zmk_settings_poll(const gmk_zmk_update_env_t *env) {
    k_spinlock_key_t key     = k_spin_lock(&job_lock);
    bool             pending = job.pending;
    k_spin_unlock(&job_lock, key);
    if (!pending) return;
    int result = -ENOTSUP;
    if (env->left && env->wired && env->writes_ready && (!job.require_usb || env->usb_configured)) result = gmk_settings_store_save(&store, job.name, job.deleting ? NULL : job.value, job.size);
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
    int result = gmk_settings_store_mount(&store, (const uint8_t *)SETTINGS_BASE, write_page, NULL);
    /* Register even on failure so generic settings calls fail explicitly.
     * Never format unknown nonblank storage or touch external Vial SPI. */
    settings_src_register(&backend);
    settings_dst_register(&backend);
    atomic_set(&ready, result == 0);
    return 0;
}
