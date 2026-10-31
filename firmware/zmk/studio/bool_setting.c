/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bool_setting.h"
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
#    include "settings_backend.h"
#endif
#include <errno.h>
#include <zephyr/sys/util.h>

static bool set_locked(struct gmk_bool_setting *setting, bool value) {
    if (setting->enabled == value) return false;
    setting->enabled    = value;
    setting->changed_at = k_uptime_get_32();
    return true;
}

static void save(struct k_work *work) {
    struct gmk_bool_setting *setting = CONTAINER_OF(work, struct gmk_bool_setting, work);
    k_spinlock_key_t         key     = k_spin_lock(&setting->lock);
    uint8_t                  value   = setting->enabled;
    k_spin_unlock(&setting->lock, key);
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
    int result = gmk_zmk_settings_save_runtime(setting->name, &value, sizeof(value));
#else
    int result = -ENOTSUP;
#endif
    key = k_spin_lock(&setting->lock);
    if (!result) setting->stored = value != 0;
    setting->saving = false;
    k_spin_unlock(&setting->lock, key);
}

void gmk_bool_setting_init(struct gmk_bool_setting *setting, const char *name, bool default_value) {
    *setting = (struct gmk_bool_setting){.enabled = default_value, .stored = default_value, .name = name};
    k_work_init(&setting->work, save);
}

bool gmk_bool_setting_get(struct gmk_bool_setting *setting) {
    k_spinlock_key_t key     = k_spin_lock(&setting->lock);
    bool             enabled = setting->enabled;
    k_spin_unlock(&setting->lock, key);
    return enabled;
}

bool gmk_bool_setting_set(struct gmk_bool_setting *setting, bool value) {
    k_spinlock_key_t key     = k_spin_lock(&setting->lock);
    bool             changed = set_locked(setting, value);
    k_spin_unlock(&setting->lock, key);
    return changed;
}

bool gmk_bool_setting_toggle(struct gmk_bool_setting *setting) {
    k_spinlock_key_t key = k_spin_lock(&setting->lock);
    set_locked(setting, !setting->enabled);
    bool enabled = setting->enabled;
    k_spin_unlock(&setting->lock, key);
    return enabled;
}

int gmk_bool_setting_load(struct gmk_bool_setting *setting, size_t len, settings_read_cb read, void *ctx) {
    if (len != 1) return -EINVAL;
    uint8_t value;
    ssize_t result = read(ctx, &value, sizeof(value));
    if (result != 1) return result < 0 ? (int)result : -EINVAL;
    if (value > 1) return -EINVAL;
    k_spinlock_key_t key = k_spin_lock(&setting->lock);
    setting->enabled = setting->stored = value != 0;
    k_spin_unlock(&setting->lock, key);
    return 0;
}

void gmk_bool_setting_poll(struct gmk_bool_setting *setting, bool writes_allowed) {
    if (!writes_allowed) return;
    uint32_t         now   = k_uptime_get_32();
    k_spinlock_key_t key   = k_spin_lock(&setting->lock);
    bool             queue = setting->enabled != setting->stored && !setting->saving && (uint32_t)(now - setting->changed_at) >= 250 && (!setting->attempted || (uint32_t)(now - setting->last_try) >= 1000);
    if (queue) {
        setting->saving = setting->attempted = true;
        setting->last_try                    = now;
    }
    k_spin_unlock(&setting->lock, key);
    /* The runtime save waits for the storage worker, so dispatch separately. */
    if (queue) k_work_submit(&setting->work);
}
