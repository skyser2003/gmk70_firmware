/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL UINT8_MAX
#include "../../firmware/zmk/controls/bindings.c"
#if CONFIG_ZMK_BEHAVIOR_METADATA
#    include "../../firmware/zmk/controls/metadata.c"
#endif
#include "../../firmware/zmk/controls/behavior.c"
#include "../../firmware/zmk/recovery/behavior.c"

static bool                        left = true, gui, mac, usb_sleep, flip, nkro;
static unsigned                    preference_writes, forwarded, requests, map_writes;
static enum gmk_recovery_action    requested;
static struct zmk_behavior_binding map[2][4];
uint16_t                           zmk_behavior_get_local_id(const char *name) {
    return !strcmp(name, "gmk_controls") ? 77 : UINT16_MAX;
}
bool gmk_wired_is_left(void) {
    return left;
}
static int set(bool *flag, bool value) {
    *flag = value;
    ++preference_writes;
    return 0;
}
int gmk_controls_gui_lock_set(bool value) {
    return set(&gui, value);
}
int gmk_controls_gui_lock_toggle(void) {
    return set(&gui, !gui);
}
int gmk_controls_mac_set(bool value) {
    return set(&mac, value);
}
int gmk_controls_mac_toggle(void) {
    return set(&mac, !mac);
}
bool gmk_controls_usb_sleep(void) {
    return usb_sleep;
}
int gmk_controls_usb_sleep_set(bool value) {
    return set(&usb_sleep, value);
}
int gmk_controls_sleep_selector_set(uint8_t value) {
    (void)value;
    ++preference_writes;
    return 0;
}
int gmk_flip_toggle(void) {
    return set(&flip, !flip);
}
int gmk_nkro_set(bool value) {
    return set(&nkro, value);
}
int gmk_nkro_toggle(void) {
    return set(&nkro, !nkro);
}
int gmk_recovery_request(enum gmk_recovery_action action) {
    ++requests;
    requested = action;
    return 0;
}
int gmk_recovery_settings_reset(void) {
    return gmk_recovery_request(GMK_RECOVERY_SETTINGS_RESET);
}
const char *__real_zmk_behavior_find_behavior_name_from_local_id(uint16_t id) {
    return id == 12 ? "kp" : id == 77 ? "gmk_controls" : NULL;
}
const struct zmk_behavior_binding *zmk_keymap_get_layer_binding_at_idx(uint8_t layer, uint16_t index) {
    return &map[layer][index];
}
int __real_zmk_keymap_set_layer_binding_at_idx(uint8_t layer, uint16_t index, struct zmk_behavior_binding binding) {
    ++map_writes;
    map[layer][index] = binding;
    return 0;
}
#ifndef CONFIG_GMK70_RADIO
int zmk_keymap_set_layer_binding_at_idx(uint8_t layer, uint16_t index, struct zmk_behavior_binding binding) {
    return __real_zmk_keymap_set_layer_binding_at_idx(layer, index, binding);
}
#endif
int zmk_split_central_invoke_behavior(uint8_t source_id, const struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event, bool pressed) {
    assert(left && source_id == 0 && !strcmp(binding->behavior_dev, "gmk_controls"));
    ++forwarded;
    left                               = false; /* Native peripheral executes on the original event source. */
    struct zmk_behavior_binding copy   = *binding;
    int                         result = pressed ? controls_pressed(&copy, event) : controls_released(&copy, event);
    left                               = true;
    return result;
}
static int action(uint32_t code, uint8_t source_id, uint32_t position, int64_t time, bool pressed) {
    struct zmk_behavior_binding       binding = {.behavior_dev = "gmk_controls", .param1 = code};
    struct zmk_behavior_binding_event event   = {.source = source_id, .position = position, .timestamp = time};
    return pressed ? controls_pressed(&binding, event) : controls_released(&binding, event);
}
static void all_actions_and_metadata(void) {
    assert(action(0, 255, 0, 0, true) == 0 && gui);
    assert(action(2, 255, 0, 0, true) == 0 && !gui);
    assert(action(4, 255, 0, 0, true) == 0 && mac);
    assert(action(3, 255, 0, 0, true) == 0 && !mac);
    assert(action(7, 255, 0, 0, true) == 0 && usb_sleep);
    assert(action(8, 255, 0, 0, true) == 0 && !usb_sleep);
    assert(action(9, 255, 0, 0, true) == 0 && flip);
    assert(action(10, 255, 0, 0, true) == 0 && nkro);
    assert(action(12, 255, 0, 0, true) == 0 && !nkro);
    assert(action(11, 255, 0, 0, true) == 0 && nkro);
    unsigned writes = preference_writes;
    for (unsigned code = 0; code <= 12; ++code)
        assert(action(code, 255, 0, 0, false) == 0);
    assert(preference_writes == writes);
    assert(action(16, 255, 0, 0, true) == -EINVAL);
#if CONFIG_ZMK_BEHAVIOR_METADATA
    const struct behavior_parameter_metadata_set *values = control_metadata.sets;
    assert(values->param1_values_len == 16 && !values->param2_values_len);
    for (unsigned i = 0; i < 16; ++i) {
        assert(values->param1_values[i].value == i);
        assert(strlen(values->param1_values[i].display_name) >= 20);
    }
    for (unsigned i = 13; i <= 15; ++i)
        assert(strstr(values->param1_values[i].display_name, "3s"));
    assert(sleep_metadata.sets->param1_values_len == 8);
    assert(gmk70_menu_metadata[2].sets->param1_values_len == 7);
    assert(gmk70_menu_metadata[3].sets->param1_values_len == 2);
#    define CHECK_CONTROL(v, text) assert(values->param1_values[v].value == (v) && !strcmp(values->param1_values[v].display_name, text));
    GMK70_BASE_CONTROL_OPTIONS(CHECK_CONTROL)
    GMK70_EXTRA_CONTROL_OPTIONS(CHECK_CONTROL)
#    undef CHECK_CONTROL
#    define CHECK_RADIO(v, text) assert(gmk70_menu_metadata[2].sets->param1_values[v].value == (v) && !strcmp(gmk70_menu_metadata[2].sets->param1_values[v].display_name, text));
    GMK70_RADIO_OPTIONS(CHECK_RADIO)
#    undef CHECK_RADIO
#    define CHECK_BATTERY(v, text) assert(gmk70_menu_metadata[3].sets->param1_values[(v) - GMK_BATTERY_LEVEL].value == (v) && !strcmp(gmk70_menu_metadata[3].sets->param1_values[(v) - GMK_BATTERY_LEVEL].display_name, text));
    GMK70_BATTERY_OPTIONS(CHECK_BATTERY)
#    undef CHECK_BATTERY
#endif
}
static void recovery_keeps_hold_source_and_release_ownership(void) {
    for (unsigned code = 13; code <= 15; ++code) {
        for (unsigned source_id = 0; source_id <= 1; ++source_id) {
            uint8_t  source = source_id ? 255 : 0;
            unsigned before = requests, sends = forwarded;
            assert(action(code, source, 8, 100, true) == 0);
            assert(action(code, source, 8, 3099, false) == 0 && requests == before);
            assert(action(code, source, 8, 1000, true) == 0);
            assert(action(code, source, 9, 4100, false) == 0 && requests == before);
            if (code == 15) {
                recovery_test_advance(2999);
                assert(requests == before);
                recovery_test_advance(1);
                assert(requests == before + 1);
            }
            assert(action(code, source, 8, 4000, false) == 0 && requests == before + 1);
            assert(requested == (enum gmk_recovery_action)(code - 12));
            assert(source_id || code == 15 ? forwarded == sends : forwarded == sends + 5);
        }
    }
    assert(action(13, 255, 42, 100, true) == 0);
    zmk_event_t release = {.position_event = true, .data = {.source = 255, .position = 42, .timestamp = 3100, .state = false}};
    unsigned    before  = requests;
    assert(registered_listener->callback(&release) == ZMK_EV_EVENT_BUBBLE);
    assert(requests == before + 1 && requested == GMK_RECOVERY_RESET);
}
static void old_bindings_ids_and_keymap_migrate_in_ram(void) {
    const struct {
        const char *name;
        uint32_t    old_value, new_value;
    } cases[] = {{"gmk_flip", 0, 9}, {"gmk_nkro", 0, 10}, {"gmk_nkro", 1, 11}, {"gmk_nkro", 2, 12}, {"gmk_recovery", 1, 13}, {"gmk_recovery", 2, 14}, {"gmk_recovery", 3, 15}};
    for (unsigned i = 0; i < ARRAY_SIZE(cases); ++i) {
        struct zmk_behavior_binding binding = {.behavior_dev = cases[i].name, .param1 = cases[i].old_value, .local_id = 99};
        assert(gmk_controls_normalize_binding(&binding) == 1 && binding.param1 == cases[i].new_value);
        assert(!strcmp(binding.behavior_dev, "gmk_controls") && binding.local_id == 77);
        assert(gmk_controls_normalize_binding(&binding) == 0);
        struct zmk_behavior_binding old = {.behavior_dev = cases[i].name, .param1 = cases[i].old_value};
        map[i / 4][i % 4]               = old;
    }
    map[1][3] = (struct zmk_behavior_binding){.behavior_dev = "kp", .param1 = 4, .param2 = 9};
    assert(!strcmp(test_settings_handler.name, "keymap/gmk_controls") && test_settings_handler.priority > 0);
    assert(test_settings_handler.commit() == 0 && gmk_controls_bindings_ready());
    assert(map_writes == 7 && !strcmp(map[1][3].behavior_dev, "kp") && map[1][3].param2 == 9);
    gmk_controls_migrate_keymap();
    assert(map_writes == 7);
    gmk_controls_prepare_legacy_load();
    for (unsigned i = 0; i < ARRAY_SIZE(legacy_names); ++i) {
        gmk_controls_note_legacy_id((uint16_t)(24 + i), (const uint8_t *)legacy_names[i], strlen(legacy_names[i]));
        assert(!strcmp(__wrap_zmk_behavior_find_behavior_name_from_local_id((uint16_t)(24 + i)), legacy_names[i]));
    }
    assert(!strcmp(__wrap_zmk_behavior_find_behavior_name_from_local_id(12), "kp"));
    assert(__wrap_zmk_behavior_find_behavior_name_from_local_id(99) == NULL);
    const struct zmk_behavior_binding bad[]  = {{.behavior_dev = "gmk_nkro", .param1 = 3}, {.behavior_dev = "gmk_flip", .param1 = 1}, {.behavior_dev = "gmk_recovery", .param1 = 0}, {.behavior_dev = "gmk_recovery", .param1 = 1, .param2 = 1}};
    unsigned                          before = requests;
    for (unsigned i = 0; i < ARRAY_SIZE(bad); ++i) {
        struct zmk_behavior_binding copy = bad[i];
        assert(gmk_controls_normalize_binding(&copy) == -EINVAL);
        assert(copy.param1 == bad[i].param1 && copy.behavior_dev == bad[i].behavior_dev);
    }
    assert(requests == before);
    gmk_controls_migrate_keymap();
    assert(__wrap_zmk_behavior_find_behavior_name_from_local_id(24) == NULL);
}
int main(void) {
#if CONFIG_ZMK_BEHAVIOR_METADATA
    assert(gmk70_menu_metadata_init() == 0);
#endif
    all_actions_and_metadata();
    recovery_keeps_hold_source_and_release_ownership();
    old_bindings_ids_and_keymap_migrate_in_ram();
    puts("Merged controls: all actions, descriptive metadata, per-half guarded recovery and legacy RAM migration passed");
    return 0;
}
