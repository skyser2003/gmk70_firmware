/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <zephyr/settings/settings.h>
#include <zmk/physical_layouts.h>
#include "../../firmware/zmk/radio/behavior.c"

static bool           settings_block;
static bool           map_block;
static int            reload_result;
static int            map_result = 16;
static unsigned       settings_loads, settings_deletes;
static const uint32_t native_position_map[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

static void native_delay(void) {
    assert(!radio_test_locked);
    radio_test_time += 3000;
    gmk_radio_poll();
    run_work();
    assert(!core.pending_pair);
}
int zmk_physical_layouts_get_selected_to_stock_position_map(const uint32_t **map) {
    if (map_block) native_delay();
    *map = native_position_map;
    return map_result;
}
size_t strlcpy(char *dest, const char *src, size_t size) {
    const size_t length = strlen(src);
    if (size) {
        const size_t copied = length < size - 1 ? length : size - 1;
        memcpy(dest, src, copied);
        dest[copied] = '\0';
    }
    return length;
}
zmk_behavior_local_id_t zmk_behavior_get_local_id(const char *name) {
    return name && strcmp(name, "saved") == 0 ? 2 : 1;
}
const char *zmk_behavior_find_behavior_name_from_local_id(zmk_behavior_local_id_t id) {
    return id == 2 ? "saved" : "stock";
}
int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event, bool pressed) {
    (void)binding;
    (void)event;
    (void)pressed;
    return 0;
}
int settings_name_steq(const char *name, const char *key, const char **next) {
    const size_t length = strlen(key);
    if (strncmp(name, key, length) || (name[length] != '/' && name[length] != '\0')) return 0;
    *next = name[length] == '/' ? name + length + 1 : NULL;
    return 1;
}
static ssize_t read_saved_binding(void *context, void *dest, size_t len) {
    (void)context;
    /* Native packed settings record: local ID followed by two parameters. */
    const uint8_t saved[10] = {2, 0, 42, 0, 0, 0, 0, 0, 0, 0};
    assert(len <= sizeof(saved));
    memcpy(dest, saved, len);
    return (ssize_t)len;
}
int settings_load_subtree(const char *subtree) {
    assert(strcmp(subtree, "keymap") == 0 && !radio_test_locked);
    ++settings_loads;
    if (settings_block) native_delay();
    if (reload_result < 0) return reload_result;
    assert(radio_native_settings.set("l/0/0", 10, read_saved_binding, NULL) == 0);
    assert(radio_native_settings.commit() == 0);
    return 0;
}
int settings_load_subtree_direct(const char *subtree, settings_load_direct_cb callback, void *param) {
    assert(strcmp(subtree, "keymap") == 0 && !radio_test_locked);
    /* Every fixture layer/position is present, initializing native reset's scratch bitmap. */
    for (unsigned layer = 0; layer < 3; ++layer) {
        for (unsigned position = 0; position < 16; ++position) {
            char key[16];
            assert(snprintf(key, sizeof(key), "l/%u/%u", layer, position) > 0);
            assert(callback(key, 10, read_saved_binding, NULL, param) == 0);
        }
    }
    return 0;
}
int settings_delete(const char *key) {
    assert(strncmp(key, "keymap/", 7) == 0 && !radio_test_locked);
    ++settings_deletes;
    if (settings_block) {
        settings_block = false;
        native_delay();
    }
    return 0;
}
int settings_save_one(const char *key, const void *value, size_t len) {
    (void)key;
    (void)value;
    (void)len;
    return 0;
}

static void native_hold(void) {
    settings_block = map_block = false;
    reload_result              = 0;
    map_result                 = 16;
    start(true);
    assert(keymap_init() == 0);
    gmk_radio_poll();
    radio_test_time = 50;
    gmk_radio_poll();
    run_work();
    settle();
    connect_radio();
    struct zmk_behavior_binding             binding = {.behavior_dev = "radio", .param1 = 2};
    const struct zmk_behavior_binding_event event   = {.position = 9};
    assert(registered_api->binding_pressed(&binding, event) == 0);
    run_work();
    settle();
}
static void native_cancellation_finished(void) {
    assert(!core.pending_pair);
    radio_test_time += 3000;
    gmk_radio_poll();
    run_work();
    assert(!core.pending_pair);
    settle();
    const uint8_t pressed[8] = {0, 0, 4};
    assert(core.connection == GMK_RADIO_CONNECTED);
    assert(gmk_radio_submit_keyboard(pressed) == 0);
}
static void native_discard(void) {
    const unsigned loads = settings_loads;
    assert(zmk_keymap_discard_changes() == 0);
    assert(settings_loads == loads + 1);
    const struct zmk_behavior_binding *binding = zmk_keymap_get_layer_binding_at_idx(0, 0);
    assert(binding && strcmp(binding->behavior_dev, "saved") == 0 && binding->param1 == 42);
}
static void native_reset(void) {
    const unsigned deletes = settings_deletes;
    assert(zmk_keymap_reset_settings() == 0);
    assert(settings_deletes > deletes);
    const struct zmk_behavior_binding *binding = zmk_keymap_get_layer_binding_at_idx(0, 0);
    assert(binding && strcmp(binding->behavior_dev, "stock") == 0 && binding->param1 == 0);
}
static void native_replacements_cancel_at_every_stage(void) {
    void (*const replacements[])(void) = {native_reset, native_discard};
    for (unsigned operation = 0; operation < 2; ++operation) {
        for (unsigned stage = 0; stage < 4; ++stage) {
            native_hold();
            if (stage == 1 || stage == 2) {
                radio_test_time = 3050;
                gmk_radio_poll();
                assert(work.pending);
                if (stage == 2) {
                    run_work();
                    assert(core.pending_pair);
                }
            }
            settings_block = stage == 3;
            replacements[operation]();
            native_cancellation_finished();
        }
    }
}
static void native_failed_and_noop_operations_cancel_before_native_work(void) {
    native_hold();
    settings_block = true;
    reload_result  = -EIO;
    assert(zmk_keymap_discard_changes() == -EIO);
    native_cancellation_finished();
    native_hold();
    native_discard();
    assert(gmk_radio_hold(2, 9) == 0);
    run_work();
    settle();
    settings_block = true;
    native_discard();
    native_cancellation_finished();
    for (unsigned failed = 0; failed < 2; ++failed) {
        native_hold();
        const struct zmk_behavior_binding unchanged = *zmk_keymap_get_layer_binding_at_idx(0, 0);
        map_block                                   = true;
        map_result                                  = failed ? -EIO : 16;
        assert(zmk_keymap_set_layer_binding_at_idx(0, 0, unchanged) == (failed ? -EIO : 0));
        map_block = false;
        native_cancellation_finished();
    }
    native_hold();
    assert(zmk_keymap_move_layer(0, 0) == 0);
    native_cancellation_finished();
    native_hold();
    assert(zmk_keymap_restore_layer(99, 0) == -ENODEV);
    native_cancellation_finished();
}
static void native_layer_selection_changes_cancel(void) {
    for (unsigned operation = 0; operation < 4; ++operation) {
        native_hold();
        switch (operation) {
            case 0:
                assert(zmk_keymap_move_layer(0, 1) == 0);
                assert(zmk_keymap_layer_index_to_id(1) == 0);
                break;
            case 1:
                assert(zmk_keymap_add_layer() == 2);
                assert(zmk_keymap_layer_index_to_id(2) == 2);
                break;
            case 2:
                assert(zmk_keymap_remove_layer(0) == 0);
                assert(zmk_keymap_layer_index_to_id(0) == 1);
                break;
            case 3:
                assert(zmk_keymap_restore_layer(2, 0) == 0);
                assert(zmk_keymap_layer_index_to_id(0) == 2);
                break;
        }
        native_cancellation_finished();
    }
}
static void native_physical_release(uint32_t position) {
    const zmk_event_t event = {.position_event = true, .data = {.position = position, .state = false}};
    assert(registered_listener->callback(&event) == ZMK_EV_EVENT_BUBBLE);
}
static void native_edits_and_independent_physical_releases(void) {
    native_hold();
    const struct zmk_behavior_binding replacement = {.behavior_dev = "replacement", .param1 = 7};
    assert(zmk_keymap_set_layer_binding_at_idx(1, 3, replacement) == 0);
    assert(zmk_keymap_get_layer_binding_at_idx(1, 3)->param1 == 7);
    native_cancellation_finished();
    native_hold();
    native_physical_release(10);
    radio_test_time = 3050;
    gmk_radio_poll();
    run_work();
    assert(core.pending_pair);
    native_physical_release(9);
    native_cancellation_finished();
    native_hold();
    native_discard();
    assert(gmk_radio_hold(3, 10) == 0);
    run_work();
    settle();
    native_physical_release(9);
    radio_test_time += 3000;
    gmk_radio_poll();
    run_work();
    assert(core.pending_pair);
    native_physical_release(10);
    assert(!core.pending_pair);
}
static void native_keymap_remapping_tests(void) {
    native_replacements_cancel_at_every_stage();
    native_failed_and_noop_operations_cancel_before_native_work();
    native_layer_selection_changes_cancel();
    native_edits_and_independent_physical_releases();
    puts("Radio native keymap: Discard/Reset, ordering, queued/pending pairs, blocking/failure/no-op and physical release passed");
}
