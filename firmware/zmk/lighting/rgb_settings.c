/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <zmk/rgb_underglow.h>

/* Pinned native RGB behavior resolves hue/saturation/brightness commands to
 * set_hsb(), which changes RAM without scheduling its normal settings save.
 * Retain upstream untouched and use the same debounced persistence path. */
int zmk_rgb_underglow_save_state(void);
int __real_zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color);
int __wrap_zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color) {
    int result = __real_zmk_rgb_underglow_set_hsb(color);
    return result ? result : zmk_rgb_underglow_save_state();
}

#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
#    include "rgb_settings.h"
#    include "../studio/settings_backend.h"
#    include <errno.h>
#    include <string.h>
#    include <zephyr/kernel.h>
#    include <zephyr/settings/settings.h>

/* Opaque native rgb_underglow_state bytes at the pinned ZMK revision. The
 * native save/load fixture verifies this bound and round-trip representation.
 * Do not introduce a second RGB schema or retain a caller's mutable pointer. */
#    define RGB_NATIVE_STATE_BYTES 10
static struct k_spinlock save_lock;
static uint8_t           latest[RGB_NATIVE_STATE_BYTES];
static uint32_t          last_try;
static bool              dirty, saving, attempted;

static void save_runtime(struct k_work *work) {
    ARG_UNUSED(work);
    uint8_t          value[RGB_NATIVE_STATE_BYTES];
    k_spinlock_key_t key = k_spin_lock(&save_lock);
    memcpy(value, latest, sizeof(value));
    k_spin_unlock(&save_lock, key);
    int result = gmk_zmk_settings_save_runtime("rgb/underglow/state", value, sizeof(value));
    key        = k_spin_lock(&save_lock);
    /* A newer callback can replace the cache while storage is busy. Only
     * acknowledge the bytes that actually reached the native settings bank. */
    if (!result && !memcmp(value, latest, sizeof(value))) dirty = false;
    saving = false;
    k_spin_unlock(&save_lock, key);
}
static K_WORK_DEFINE(rgb_save_work, save_runtime);

int __real_settings_save_one(const char *name, const void *value, size_t size);
int __wrap_settings_save_one(const char *name, const void *value, size_t size) {
    if (!name || strcmp(name, "rgb/underglow/state")) return __real_settings_save_one(name, value, size);
    if (!value || size != sizeof(latest)) return -EINVAL;
    k_spinlock_key_t key = k_spin_lock(&save_lock);
    memcpy(latest, value, sizeof(latest));
    dirty = true;
    k_spin_unlock(&save_lock, key);
    /* Native RGB ignores the callback's result. Retain its debounced snapshot
     * without blocking system work on an ineligible storage transaction. */
    return 0;
}

void gmk_rgb_settings_poll(bool writes_allowed) {
    if (!writes_allowed) return;
    uint32_t         now   = k_uptime_get_32();
    k_spinlock_key_t key   = k_spin_lock(&save_lock);
    bool             queue = dirty && !saving && (!attempted || (uint32_t)(now - last_try) >= 1000);
    if (queue) {
        saving = attempted = true;
        last_try           = now;
    }
    k_spin_unlock(&save_lock, key);
    /* The backend waits for this update worker, so never call it inline. */
    if (queue && k_work_submit(&rgb_save_work) < 0) {
        key    = k_spin_lock(&save_lock);
        saving = false;
        k_spin_unlock(&save_lock, key);
    }
}
#endif
