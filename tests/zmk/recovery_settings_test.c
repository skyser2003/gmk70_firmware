/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/settings/settings.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include "../../firmware/zmk/recovery/recovery.h"

uint32_t        radio_test_time;
unsigned        radio_test_locked;
static unsigned clears, control_resets, rgb_resets, mutable_saves;
static int      bank_result;
static bool     flip = true, nkro;
int             keymap_init(void);
void            zmk_endpoint_clear_reports(void) {
    ++clears;
}
void gmk_controls_reset(void) {
    ++control_resets;
}
int gmk_rgb_settings_reset(void) {
    ++rgb_resets;
    return 0;
}
int gmk_zmk_settings_reset_local(int (*restore)(void)) {
    return bank_result ? bank_result : restore();
}
static int load_flip(const char *name, size_t size, settings_read_cb read, void *ctx) {
    assert(!strcmp(name, "state") && size == 1);
    uint8_t value;
    assert(read(ctx, &value, 1) == 1);
    flip = value;
    return 0;
}
static int load_nkro(const char *name, size_t size, settings_read_cb read, void *ctx) {
    assert(!strcmp(name, "state") && size == 1);
    uint8_t value;
    assert(read(ctx, &value, 1) == 1);
    nkro = value;
    return 0;
}
const struct settings_handler_static settings_handler_gmk_flip = {.h_set = load_flip};
const struct settings_handler_static settings_handler_gmk_nkro = {.h_set = load_nkro};
static ssize_t                       read_state(void *ctx, void *dest, size_t size) {
    memcpy(dest, ctx, size);
    return (ssize_t)size;
}
int gmk_zmk_settings_restore_state(const struct settings_handler_static *handler, const void *value, size_t size) {
    return handler->h_set("state", size, read_state, (void *)value);
}
int zmk_physical_layouts_get_selected_to_stock_position_map(const uint32_t **map) {
    static const uint32_t positions[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    *map                                = positions;
    return 16;
}
size_t strlcpy(char *dest, const char *src, size_t size) {
    size_t length = strlen(src);
    if (size) {
        size_t copied = length < size - 1 ? length : size - 1;
        memcpy(dest, src, copied);
        dest[copied] = '\0';
    }
    return length;
}
zmk_behavior_local_id_t zmk_behavior_get_local_id(const char *name) {
    return name ? 1 : 0;
}
const char *zmk_behavior_find_behavior_name_from_local_id(zmk_behavior_local_id_t id) {
    return id ? "stock" : NULL;
}
int settings_load_subtree(const char *subtree) {
    assert(!strcmp(subtree, "keymap"));
    /* The physical reset committed an empty bank before invoking native Discard. */
    return radio_native_settings.commit();
}
int settings_load_subtree_direct(const char *subtree, settings_load_direct_cb cb, void *param) {
    (void)subtree;
    (void)cb;
    (void)param;
    assert(false); /* Empty-bank Discard needs no native deletion scratch array. */
    return -EIO;
}
int settings_delete(const char *name) {
    (void)name;
    assert(false);
    return -EIO;
}
int settings_save_one(const char *name, const void *value, size_t size) {
    (void)value;
    (void)size;
    if (!strncmp(name, "keymap/l/", 9) || !strncmp(name, "keymap/n/", 9)) ++mutable_saves;
    return 0;
}
int settings_name_steq(const char *name, const char *key, const char **next) {
    size_t size = strlen(key);
    if (strncmp(name, key, size) || (name[size] && name[size] != '/')) return 0;
    *next = name[size] ? name + size + 1 : NULL;
    return 1;
}
int main(void) {
    assert(!keymap_init());
    struct zmk_behavior_binding stock[4][16];
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned pos = 0; pos < 16; ++pos)
            stock[i][pos] = *zmk_keymap_get_layer_binding_at_idx(i, pos);
    struct zmk_behavior_binding custom = {.behavior_dev = "stock", .param1 = 42};
    assert(!zmk_keymap_set_layer_binding_at_idx(0, 0, custom));
    assert(!zmk_keymap_set_layer_name(0, "User map", 8));
    assert(!zmk_keymap_move_layer(0, 1));
    assert(!zmk_keymap_layer_to(2, false));
    bank_result = -EBUSY;
    assert(gmk_recovery_settings_reset() == -EBUSY);
    assert(!clears && !control_resets && !rgb_resets && flip && !nkro);
    assert(zmk_keymap_get_layer_binding_at_idx(0, 0)->param1 == 42);
    bank_result = 0;
    assert(!gmk_recovery_settings_reset());
    assert(clears == 2 && control_resets == 1 && rgb_resets == 1 && !flip && nkro);
    const char *names[] = {"Windows", "Windows Fn", "Mac", "Mac Fn"};
    for (unsigned i = 0; i < 4; ++i) {
        assert(zmk_keymap_layer_index_to_id(i) == i);
        assert(!strcmp(zmk_keymap_layer_name(i), names[i]));
        for (unsigned pos = 0; pos < 16; ++pos) {
            const struct zmk_behavior_binding *binding = zmk_keymap_get_layer_binding_at_idx(i, pos);
            assert(binding && binding->param1 == stock[i][pos].param1 && binding->param2 == stock[i][pos].param2);
            assert(binding->behavior_dev == stock[i][pos].behavior_dev);
        }
    }
    assert(zmk_keymap_layer_active(0) && !zmk_keymap_layer_active(2));
    assert(!zmk_keymap_save_changes());
    assert(!mutable_saves); /* No stale pending map or name resurrects after reset. */
    puts("Physical settings reset: actual native factory map/order/names, held reports, preferences and pending edits passed");
}
