/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bindings.h"
#include <dt-bindings/zmk/gmk70_controls.h>
#include <drivers/behavior.h>
#include <errno.h>
#include <string.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>
#include <zephyr/settings/settings.h>

/* These exact device names are part of already-saved behavior/local_id data.
 * Keep them as aliases, without registering duplicate Studio behaviors. */
static const char *const legacy_names[] = {"gmk_flip", "gmk_nkro", "gmk_recovery"};
static uint16_t          legacy_ids[3];
static bool              loading;
void                     gmk_controls_prepare_legacy_load(void) {
    loading = true;
}
void gmk_controls_note_legacy_id(uint16_t id, const uint8_t *name, size_t size) {
    for (unsigned i = 0; i < ARRAY_SIZE(legacy_names); ++i)
        if (strlen(legacy_names[i]) == size && !memcmp(name, legacy_names[i], size)) legacy_ids[i] = id;
}
const char *gmk_controls_legacy_name(uint16_t id) {
    if (id)
        for (unsigned i = 0; i < ARRAY_SIZE(legacy_names); ++i)
            if (legacy_ids[i] == id) return legacy_names[i];
    return NULL;
}
int gmk_controls_normalize_binding(struct zmk_behavior_binding *binding) {
    if (!binding->behavior_dev) return 0;
    unsigned kind;
    for (kind = 0; kind < ARRAY_SIZE(legacy_names); ++kind)
        if (!strcmp(binding->behavior_dev, legacy_names[kind])) break;
    if (kind == ARRAY_SIZE(legacy_names)) return 0;
    uint32_t value = binding->param1;
    if (binding->param2 || (kind == 0 && value) || (kind == 1 && value > 2) || (kind == 2 && (value < 1 || value > 3))) return -EINVAL;
    binding->param1       = kind == 0 ? GMK70_FUNCTION_FLIP : kind == 1 ? GMK70_NKRO_TOGGLE + value : GMK70_RECOVERY_RESET + value - 1;
    binding->behavior_dev = "gmk_controls";
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_LOCAL_IDS_IN_BINDINGS)
    binding->local_id = zmk_behavior_get_local_id(binding->behavior_dev);
#endif
    return 1;
}

/* Called after native settings commit, or from native Reset. Use the native
 * RAM setter; never save or rewrite storage here. */
#ifdef CONFIG_LTO
#    define gmk_migrate_set_binding gmk_native_keymap_set_layer_binding_at_idx
#elif defined(CONFIG_GMK70_RADIO)
#    define gmk_migrate_set_binding __real_zmk_keymap_set_layer_binding_at_idx
#else
#    define gmk_migrate_set_binding zmk_keymap_set_layer_binding_at_idx
#endif
int  gmk_migrate_set_binding(zmk_keymap_layer_id_t layer, uint16_t index, struct zmk_behavior_binding binding);
void gmk_controls_migrate_keymap(void) {
    for (zmk_keymap_layer_id_t layer = 0; layer < ZMK_KEYMAP_LAYERS_LEN; ++layer)
        for (uint16_t index = 0; index < ZMK_KEYMAP_LEN; ++index) {
            const struct zmk_behavior_binding *original = zmk_keymap_get_layer_binding_at_idx(layer, index);
            if (!original) continue;
            struct zmk_behavior_binding binding = *original;
            if (gmk_controls_normalize_binding(&binding) > 0) (void)gmk_migrate_set_binding(layer, index, binding);
        }
    loading = false;
}

/* Orphan IDs are needed only while native settings load/commit resolves the
 * old keymap. Never expose an unregistered device to Studio detail queries. */
const char *__real_zmk_behavior_find_behavior_name_from_local_id(zmk_behavior_local_id_t id);
const char *__wrap_zmk_behavior_find_behavior_name_from_local_id(zmk_behavior_local_id_t id) {
    const char *name = __real_zmk_behavior_find_behavior_name_from_local_id(id);
    return name ? name : loading ? gmk_controls_legacy_name(id) : NULL;
}

bool gmk_controls_bindings_ready(void) {
    return !loading;
}
static int migrated(void) {
    gmk_controls_migrate_keymap();
    return 0;
}
/* Native behavior/keymap handlers commit at priority zero. This child also
 * participates in settings_load_subtree("keymap") used by Discard. */
SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO(gmk_controls_bindings, "keymap/gmk_controls", NULL, NULL, migrated, NULL, 1);
