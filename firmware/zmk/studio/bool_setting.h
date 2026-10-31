/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

/* Internal fields: callers use the functions below after initialization. */
struct gmk_bool_setting {
    bool              enabled, stored, saving, attempted;
    uint32_t          changed_at, last_try;
    struct k_spinlock lock;
    struct k_work     work;
    const char       *name;
};

/* The name must remain valid for the setting's lifetime. Defaults are clean. */
void gmk_bool_setting_init(struct gmk_bool_setting *setting, const char *name, bool default_value);
bool gmk_bool_setting_get(struct gmk_bool_setting *setting);
/* Set returns whether the value changed; toggle returns the new value. */
bool gmk_bool_setting_set(struct gmk_bool_setting *setting, bool value);
bool gmk_bool_setting_toggle(struct gmk_bool_setting *setting);
int  gmk_bool_setting_load(struct gmk_bool_setting *setting, size_t len, settings_read_cb read, void *ctx);
/* Poll only queues work; the caller must supply the current write gates. */
void gmk_bool_setting_poll(struct gmk_bool_setting *setting, bool writes_allowed);
