/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "recovery.h"
#include "../studio/settings_backend.h"
#include "../lighting/rgb_settings.h"
#include "../controls/controls.h"
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>
#include <string.h>

/* These are Zephyr's registered static handlers from the maintained board
 * integration, not a second settings representation. */
extern const struct settings_handler_static settings_handler_gmk_flip, settings_handler_gmk_nkro;

/* Use the existing native handlers and stored representations. Resetting the
 * application bank never touches the resident, application or radio bonds. */
static int restore_defaults(void) {
    zmk_endpoint_clear_reports();
    int                      result;
    static const char *const names[] = {"Windows", "Windows Fn", "Mac", "Mac Fn"};
    for (unsigned i = 0; i < ARRAY_SIZE(names); ++i) {
        result = zmk_keymap_set_layer_name((zmk_keymap_layer_id_t)i, names[i], strlen(names[i]));
        if (result) return result;
    }
    /* The bank is already empty: native Discard reloads factory bindings and
     * ordering and clears pending edits, without redundant per-key deletion. */
    result = zmk_keymap_discard_changes();
    if (result) return result;
    (void)zmk_keymap_layer_to(0, false);
    const uint8_t disabled = 0, nkro = 1;
    gmk_controls_reset();
    result = gmk_zmk_settings_restore_state(&settings_handler_gmk_flip, &disabled, sizeof(disabled));
    if (!result) result = gmk_zmk_settings_restore_state(&settings_handler_gmk_nkro, &nkro, sizeof(nkro));
    if (!result) result = gmk_rgb_settings_reset();
    zmk_endpoint_clear_reports();
    return result;
}
int gmk_recovery_settings_reset(void) {
    return gmk_zmk_settings_reset_local(restore_defaults);
}
