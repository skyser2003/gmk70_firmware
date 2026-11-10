/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <string.h>
#include <zephyr/settings/settings.h>

struct gmk_settings_read_context {
    const uint8_t *value;
    size_t         size;
};
static inline ssize_t gmk_settings_read_value(void *context, void *data, size_t size) {
    const struct gmk_settings_read_context *record = context;
    if (size > record->size) size = record->size;
    memcpy(data, record->value, size);
    return (ssize_t)size;
}

#ifdef CONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE
#    include <drivers/behavior.h>
#    ifdef CONFIG_GMK70_MERGED_CONTROLS
#        include "../controls/bindings.h"
#    endif
#    include <errno.h>
#    include <stdio.h>
#    include <stdlib.h>
#    include <zephyr/irq.h>
#    include <zephyr/sys/iterable_sections.h>

/* Keep the native handler/counter authoritative. An orphan name is not loaded
 * into a device, but its historical numeric ID must still reserve that range.
 * Direct callbacks and unrelated subtrees must remain observational. */
static inline int gmk_settings_reserve_orphan_id(const char *key, const struct gmk_settings_read_context *record, const struct settings_load_arg *arg) {
    if (strncmp(key, "behavior/local_id/", sizeof("behavior/local_id/") - 1) || (arg && (arg->cb || (arg->subtree && !settings_name_steq(key, arg->subtree, NULL))))) return 0;
    char         *end;
    unsigned long id = strtoul(key + sizeof("behavior/local_id/") - 1, &end, 10);
    if (*end || !id || id >= UINT16_MAX) return -EINVAL;
#    ifdef CONFIG_GMK70_MERGED_CONTROLS
    gmk_controls_note_legacy_id((uint16_t)id, record->value, record->size);
#    endif
    struct zmk_behavior_local_id_map *seed   = NULL;
    bool                              known  = false;
    unsigned                          vacant = 0;
    STRUCT_SECTION_FOREACH(zmk_behavior_local_id_map, item) {
        bool matches = strlen(item->device->name) == record->size && !memcmp(item->device->name, record->value, record->size);
        known |= matches;
        if (!device_is_ready(item->device)) continue;
        if (!seed) seed = item;
        if (!item->local_id && !matches) ++vacant;
    }
    if (id + vacant >= UINT16_MAX) return -EOVERFLOW;
    if (known || !seed) return 0;
    char restore[32];
    snprintf(restore, sizeof(restore), "behavior/local_id/%u", (unsigned)seed->local_id);
    struct gmk_settings_read_context existing = {.value = (const uint8_t *)seed->device->name, .size = strlen(seed->device->name)};
    /* Both calls execute only bounded native h_set/read logic. No commit or
     * hardware write is involved; restore the public table before preemption. */
    unsigned irq      = irq_lock();
    int      result   = settings_call_set_handler(key, existing.size, gmk_settings_read_value, &existing, NULL);
    int      restored = settings_call_set_handler(restore, existing.size, gmk_settings_read_value, &existing, NULL);
    irq_unlock(irq);
    return result ? result : restored;
}
#endif
