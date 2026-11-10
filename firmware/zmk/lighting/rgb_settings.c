/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <zmk/rgb_underglow.h>
#include "../runtime/idle.h"

#if defined(CONFIG_GMK70_ZMK_STUDIO_SETTINGS) || (defined(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE) && CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE)
#    include <zephyr/kernel.h>
static struct k_spinlock save_lock;
#endif
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
static bool native_snapshot_pending;
static void mark_native_pending(void) {
    k_spinlock_key_t key    = k_spin_lock(&save_lock);
    native_snapshot_pending = true;
    k_spin_unlock(&save_lock, key);
}
#endif
#if defined(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE) && CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE
#    include <zmk/events/activity_state_changed.h>
static bool inactive, have_user_state, user_on;
int         __real_raise_zmk_activity_state_changed(struct zmk_activity_state_changed event);
#endif

#if defined(CONFIG_GMK70_ZMK_STUDIO_SETTINGS) || (defined(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE) && CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE)
int __real_zmk_rgb_underglow_on(void);
int __real_zmk_rgb_underglow_off(void);
int __real_zmk_rgb_underglow_toggle(void);

static void remember_user_state(void) {
    bool state;
    if (zmk_rgb_underglow_get_state(&state)) return;
    k_spinlock_key_t key = k_spin_lock(&save_lock);
#    if defined(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE) && CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE
    user_on         = state;
    have_user_state = true;
#    endif
#    ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
    native_snapshot_pending = true;
#    endif
    k_spin_unlock(&save_lock, key);
}
int __wrap_zmk_rgb_underglow_on(void) {
    int result = __real_zmk_rgb_underglow_on();
    remember_user_state();
    return result;
}
int __wrap_zmk_rgb_underglow_off(void) {
    int result = __real_zmk_rgb_underglow_off();
    remember_user_state();
    return result;
}
int __wrap_zmk_rgb_underglow_toggle(void) {
#    if defined(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE) && CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE
    k_spinlock_key_t key      = k_spin_lock(&save_lock);
    bool             sleeping = inactive && have_user_state, target = user_on;
    k_spin_unlock(&save_lock, key);
    int result = sleeping ? (target ? __real_zmk_rgb_underglow_off() : __real_zmk_rgb_underglow_on()) : __real_zmk_rgb_underglow_toggle();
#    else
    int result = __real_zmk_rgb_underglow_toggle();
#    endif
    remember_user_state();
    return result;
}
#endif
#if defined(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE) && CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE
int __wrap_raise_zmk_activity_state_changed(struct zmk_activity_state_changed event) {
    bool             native_on = false;
    bool             valid     = !zmk_rgb_underglow_get_state(&native_on);
    k_spinlock_key_t key       = k_spin_lock(&save_lock);
    bool             restoring = event.state == ZMK_ACTIVITY_ACTIVE && inactive && have_user_state;
    if (event.state == ZMK_ACTIVITY_ACTIVE)
        inactive = false;
    else if ((event.state == ZMK_ACTIVITY_IDLE || event.state == ZMK_ACTIVITY_SLEEP) && !inactive) {
        inactive = true;
        if (valid) {
            user_on         = native_on;
            have_user_state = true;
#    ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
            native_snapshot_pending = true;
#    endif
        }
    }
    k_spin_unlock(&save_lock, key);
    int result = __real_raise_zmk_activity_state_changed(event);
    if (restoring) {
        key         = k_spin_lock(&save_lock);
        bool target = user_on;
        k_spin_unlock(&save_lock, key);
        /* Native auto_state retains its own pre-idle snapshot. Reconcile an
         * explicit user change made while idle using the same native APIs. */
        if (!zmk_rgb_underglow_get_state(&native_on) && native_on != target) {
            if (target)
                (void)__real_zmk_rgb_underglow_on();
            else
                (void)__real_zmk_rgb_underglow_off();
        }
    }
    return result;
}
#endif

/* Pinned native RGB behavior resolves hue/saturation/brightness commands to
 * set_hsb(), which changes RAM without scheduling its normal settings save.
 * Retain upstream untouched and use the same debounced persistence path. */
int zmk_rgb_underglow_save_state(void);
int __real_zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color);
int __wrap_zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color) {
    int result = __real_zmk_rgb_underglow_set_hsb(color);
    if (result) return result;
    result = zmk_rgb_underglow_save_state();
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
    if (!result) mark_native_pending();
#endif
    return result;
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
enum { RGB_NATIVE_STATE_BYTES = 10, RGB_NATIVE_ON_OFFSET = 8 };
static uint8_t  latest[RGB_NATIVE_STATE_BYTES];
static uint32_t last_try;
static bool     dirty, saving, attempted;

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
    gmk_service_notify();
}
static K_WORK_DEFINE(rgb_save_work, save_runtime);

int __real_settings_save_one(const char *name, const void *value, size_t size);
int __wrap_settings_save_one(const char *name, const void *value, size_t size) {
    if (!name || strcmp(name, "rgb/underglow/state")) return __real_settings_save_one(name, value, size);
    if (!value || size != sizeof(latest)) return -EINVAL;
    k_spinlock_key_t key = k_spin_lock(&save_lock);
    memcpy(latest, value, sizeof(latest));
#    if defined(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE) && CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE
    /* This is the pinned native image, including its animation/padding bytes.
     * Only temporary automatic off is projected back to the user's on flag. */
    if (inactive && have_user_state) latest[RGB_NATIVE_ON_OFFSET] = user_on ? 1 : 0;
#    endif
    native_snapshot_pending = false;
    dirty                   = true;
    k_spin_unlock(&save_lock, key);
    gmk_service_notify();
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
bool gmk_rgb_settings_dirty(void) {
    k_spinlock_key_t key     = k_spin_lock(&save_lock);
    bool             pending = dirty || saving || native_snapshot_pending;
    k_spin_unlock(&save_lock, key);
    return pending;
}
#endif
