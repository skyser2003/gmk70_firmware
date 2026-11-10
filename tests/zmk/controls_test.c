/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>

#include "../../firmware/zmk/controls/controls.c"
#include "../../firmware/zmk/lighting/battery_view.h"
#include "../../firmware/zmk/battery/battery.h"
#include "../../firmware/zmk/radio/radio.h"
#include <zmk/hid.h>
#include <zmk/hid_indicators.h>
#include <zmk/events/endpoint_changed.h>

uint32_t        radio_test_time;
unsigned        radio_test_locked;
static bool     left = true;
static uint32_t layers;
static unsigned report_count, save_count;
static int      save_result;
static uint8_t  saved[4];
static bool     change_during_save;
static uint8_t  physical_host = 3, restored_bt, requested_bt, reported_channel;

bool gmk_wired_is_left(void) {
    return left;
}
uint8_t gmk_host_switch(void) {
    return physical_host;
}
int gmk_radio_restore_last_bt(uint8_t slot) {
    assert(!radio_test_locked);
    restored_bt = slot;
    return 0;
}
int gmk_radio_select(uint8_t slot, bool pair) {
    assert(!radio_test_locked && !pair);
    requested_bt = slot;
    return 0;
}
void gmk_radio_get(struct gmk_radio_status *status) {
    *status = (struct gmk_radio_status){.selected = reported_channel};
}
bool zmk_keymap_layer_active(uint8_t layer) {
    return (layers & (UINT32_C(1) << layer)) != 0;
}
int zmk_keymap_layer_activate(uint8_t layer, bool locking) {
    assert(!locking);
    bool changed = !zmk_keymap_layer_active(layer);
    layers |= UINT32_C(1) << layer;
    return changed ? raise_layer_state_changed(layer, true, false) : 0;
}
int zmk_keymap_layer_deactivate(uint8_t layer, bool locking) {
    assert(!locking);
    bool changed = zmk_keymap_layer_active(layer);
    layers &= ~(UINT32_C(1) << layer);
    return changed ? raise_layer_state_changed(layer, false, false) : 0;
}
int zmk_endpoint_send_report(uint16_t page) {
    assert(!radio_test_locked && page == 7);
    ++report_count;
    return 0;
}
struct zmk_endpoint_instance zmk_endpoint_get_selected(void) {
    return (struct zmk_endpoint_instance){.transport = ZMK_TRANSPORT_USB};
}
int zmk_endpoint_instance_to_index(struct zmk_endpoint_instance endpoint) {
    assert(endpoint.transport == ZMK_TRANSPORT_USB);
    return 0;
}
int gmk_zmk_settings_save_runtime(const char *name, const void *data, size_t size) {
    assert(!radio_test_locked && !strcmp(name, "gmk_controls/state") && size == sizeof(saved));
    memcpy(saved, data, size);
    ++save_count;
    if (change_during_save) {
        change_during_save = false;
        assert(gmk_controls_usb_sleep_set(false) == 0);
    }
    return save_result;
}

/* Native event definitions and dispatch exercise the pinned event contracts. */
ZMK_EVENT_IMPL(zmk_hid_indicators_changed);
ZMK_EVENT_IMPL(zmk_layer_state_changed);
ZMK_EVENT_IMPL(zmk_endpoint_changed);
int zmk_event_manager_raise(zmk_event_t *event) {
    return zmk_listener_gmk_controls.callback(event);
}

void controls_native_led_work_run(void);

static void run_work(struct k_work *work) {
    assert(work->pending);
    work->pending = false;
    work->handler(work);
}
static void reset_fixture(void) {
    left         = true;
    layers       = 1;
    report_count = save_count = radio_test_time = 0;
    save_result                                 = 0;
    change_during_save                          = false;
    physical_host                               = 3;
    restored_bt = requested_bt = reported_channel = 0;
    assert(controls_test_init() == 0);
    zmk_hid_keyboard_clear();
    struct zmk_hid_led_report_body leds = {.leds = 0};
    zmk_hid_indicators_process_report(&leds, zmk_endpoint_get_selected());
    controls_native_led_work_run();
}
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
struct read_context {
    uint8_t bytes[4];
    ssize_t result;
};
static ssize_t read_value(void *ctx, void *dst, size_t size) {
    const struct read_context *read = ctx;
    assert(size == sizeof(read->bytes));
    if (read->result > 0) memcpy(dst, read->bytes, (size_t)read->result);
    return read->result;
}
#endif
static void assert_pixel(const uint8_t *rgb, unsigned index, uint8_t r, uint8_t g, uint8_t b) {
    assert(rgb[3 * index] == r && rgb[3 * index + 1] == g && rgb[3 * index + 2] == b);
}
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
static void test_preferences_validate_and_restore(void) {
    reset_fixture();
    assert(!gmk_controls_gui_locked() && !gmk_controls_mac_mode());
    assert(gmk_controls_bt_slot() == 1 && gmk_controls_sleep_selector() == 7);
    assert(!gmk_controls_usb_sleep() && gmk_controls_sleep_timeout_ms() == 300000);
    assert(gmk_controls_bt_slot_set(0) == -EINVAL && gmk_controls_bt_slot_set(6) == -EINVAL);
    assert(gmk_controls_sleep_selector_set(8) == -EINVAL);
    assert(!gmk_controls_dirty());
    assert(gmk_controls_gui_lock_set(true) == 0);
    assert(gmk_controls_mac_set(true) == 0);
    assert(gmk_controls_bt_slot_set(5) == 0);
    assert(gmk_controls_sleep_selector_set(6) == 0);
    assert(gmk_controls_usb_sleep_set(true) == 0);
    assert(gmk_controls_dirty());
    radio_test_time = 249;
    gmk_controls_poll(true);
    assert(save_count == 0 && !preferences.work.pending);
    radio_test_time = 250;
    gmk_controls_poll(false);
    assert(!preferences.work.pending);
    gmk_controls_poll(true);
    assert(preferences.work.pending);
    run_work(&preferences.work);
    assert(save_count == 1 && !gmk_controls_dirty());
    struct read_context good = {.result = 4};
    memcpy(good.bytes, saved, sizeof(saved));
    reset_fixture();
    assert(controls_test_settings.set("unknown", 4, read_value, &good) == -ENOENT);
    assert(controls_test_settings.set("state", 3, read_value, &good) == -EINVAL);
    for (unsigned field = 0; field < 4; ++field) {
        struct read_context bad = good;
        bad.bytes[field]        = 0xff;
        assert(controls_test_settings.set("state", 4, read_value, &bad) == -EINVAL);
        assert(!gmk_controls_dirty() && !gmk_controls_gui_locked());
    }
    struct read_context short_read = good;
    short_read.result              = 2;
    assert(controls_test_settings.set("state", 4, read_value, &short_read) == -EINVAL);
    short_read.result = -EIO;
    assert(controls_test_settings.set("state", 4, read_value, &short_read) == -EIO);
    assert(controls_test_settings.set("state", 4, read_value, &good) == 0);
    assert(controls_test_settings.commit() == 0);
    assert(gmk_controls_gui_locked() && gmk_controls_mac_mode() && gmk_controls_usb_sleep());
    assert(gmk_controls_bt_slot() == 5 && gmk_controls_sleep_selector() == 6);
    assert(gmk_controls_sleep_timeout_ms() == 1800000 && zmk_keymap_layer_active(2));
    assert(!gmk_controls_dirty());
}
static void test_shared_worker_retry_and_concurrent_change(void) {
    reset_fixture();
    assert(gmk_controls_usb_sleep_set(true) == 0);
    save_result     = -EBUSY;
    radio_test_time = 250;
    gmk_controls_poll(true);
    run_work(&preferences.work);
    assert(gmk_controls_dirty());
    radio_test_time = 1249;
    gmk_controls_poll(true);
    assert(!preferences.work.pending);
    radio_test_time    = 1250;
    save_result        = 0;
    change_during_save = true;
    gmk_controls_poll(true);
    run_work(&preferences.work);
    assert(gmk_controls_dirty() && !gmk_controls_usb_sleep());
    radio_test_time = 2250;
    gmk_controls_poll(true);
    run_work(&preferences.work);
    assert(!gmk_controls_dirty() && save_count == 3);
}
static void test_loaded_bluetooth_slot_survives_early_radio_poll(void) {
    for (unsigned early_poll = 0; early_poll < 2; ++early_poll) {
        reset_fixture();
        physical_host = 2;
        if (early_poll) gmk_controls_radio_state(1, GMK_RADIO_CONNECTED);
        struct read_context saved_slot = {.bytes = {1, 0, 5, 7}, .result = 4};
        assert(!controls_test_settings.set("state", 4, read_value, &saved_slot));
        /* Old-host publication can precede both commit and asynchronous
         * selection. Neither edge may overwrite the restored preference. */
        gmk_controls_radio_state(1, GMK_RADIO_CONNECTED);
        assert(gmk_controls_bt_slot() == 5);
        assert(!controls_test_settings.commit());
        assert(restored_bt == 5 && requested_bt == 5);
        gmk_controls_radio_state(1, GMK_RADIO_CONNECTED);
        assert(gmk_controls_bt_slot() == 5 && !gmk_controls_dirty());
        gmk_controls_radio_state(5, GMK_RADIO_CONNECTED);
        gmk_controls_radio_state(3, GMK_RADIO_CONNECTED);
        assert(gmk_controls_bt_slot() == 3 && gmk_controls_dirty());
    }
    reset_fixture();
    struct read_context saved_slot = {.bytes = {1, 0, 5, 7}, .result = 4};
    assert(!controls_test_settings.set("state", 4, read_value, &saved_slot));
    assert(!controls_test_settings.commit());
    assert(restored_bt == 5 && requested_bt == 0);
    gmk_controls_radio_state(1, GMK_RADIO_CONNECTED);
    assert(gmk_controls_bt_slot() == 5);
}
#else
static void test_no_backend_retains_unsaved_preferences(void) {
    reset_fixture();
    assert(gmk_controls_usb_sleep_set(true) == 0);
    radio_test_time = 250;
    gmk_controls_poll(true);
    run_work(&preferences.work);
    assert(gmk_controls_dirty() && gmk_controls_usb_sleep() && save_count == 0);
    radio_test_time = 300;
    gmk_controls_poll(true);
    assert(!preferences.work.pending);
}
#endif
static void test_debounce_spans_uptime_rollover(void) {
    reset_fixture();
    radio_test_time = UINT32_MAX - 99;
    assert(gmk_controls_usb_sleep_set(true) == 0);
    radio_test_time = 149;
    gmk_controls_poll(true);
    assert(!preferences.work.pending);
    radio_test_time = 150;
    gmk_controls_poll(true);
    assert(preferences.work.pending);
    run_work(&preferences.work);
}
static void test_projection_preserves_native_held_gui(void) {
    reset_fixture();
    assert(zmk_hid_register_mods(0x88) > 0);
    assert(gmk_controls_project_modifiers(zmk_hid_get_keyboard_report()->body.modifiers | 0x01) == 0x89);
    assert(gmk_controls_gui_lock_set(true) == 0);
    assert(gmk_controls_project_modifiers(zmk_hid_get_keyboard_report()->body.modifiers | 0x01) == 0x01);
    assert(zmk_hid_get_keyboard_report()->body.modifiers == 0x88);
    assert(gmk_controls_mac_set(true) == 0);
    assert(gmk_controls_project_modifiers(zmk_hid_get_keyboard_report()->body.modifiers) == 0x88);
    assert(gmk_controls_mac_set(false) == 0);
    assert(zmk_hid_unregister_mods(0x88) > 0);
    assert(gmk_controls_gui_lock_toggle() == 0);
    assert(gmk_controls_project_modifiers(zmk_hid_get_keyboard_report()->body.modifiers) == 0);
    assert(gmk_controls_gui_lock_set(true) == 0);
    assert(zmk_hid_register_mods(0x08) > 0);
    assert(gmk_controls_project_modifiers(zmk_hid_get_keyboard_report()->body.modifiers) == 0);
    assert(gmk_controls_gui_lock_set(false) == 0);
    assert(gmk_controls_project_modifiers(zmk_hid_get_keyboard_report()->body.modifiers) == 0x08);
    assert(zmk_hid_unregister_mods(0x08) > 0);
    assert(report_count >= 4);
}
static void test_native_layer_changes_preserve_other_layers(void) {
    reset_fixture();
    layers |= (1U << 1) | (1U << 3) | (1U << 4);
    assert(gmk_controls_mac_set(true) == 0);
    assert(layers == 31);
    assert(gmk_controls_mac_set(false) == 0);
    assert(layers == 27);
    assert(zmk_keymap_layer_activate(2, false) == 0);
    assert(gmk_controls_mac_mode());
    assert(zmk_keymap_layer_deactivate(2, false) == 0);
    assert(!gmk_controls_mac_mode());
    assert(gmk_controls_mac_toggle() == 0 && gmk_controls_mac_mode());
    assert(gmk_controls_mac_toggle() == 0 && !gmk_controls_mac_mode());
}
static void test_native_indicators_and_overlay_precedence(void) {
    reset_fixture();
    uint8_t                        rgb[3 * GMK_LIGHT_PIXELS] = {0};
    struct zmk_hid_led_report_body leds                      = {.leds = 2};
    uint32_t                       old_token                 = gmk_controls_visual_token(0);
    zmk_hid_indicators_process_report(&leds, zmk_endpoint_get_selected());
    controls_native_led_work_run();
    assert(gmk_controls_visual_token(0) != old_token);
    assert(gmk_controls_gui_lock_set(true) == 0);
    gmk_controls_overlay(rgb, GMK_LIGHT_PIXELS, 0, true, true);
    assert_pixel(rgb, 18, 80, 80, 80);
    assert_pixel(rgb, 73, 80, 80, 80);
    struct gmk_battery_view   view   = {0};
    struct gmk_battery_status status = {.percentage = 50, .valid = true};
    assert(gmk_battery_view_set(&view, GMK_BATTERY_LEVEL, 1, true) == 0);
    gmk_battery_view_overlay(&view, &status, rgb);
    assert_pixel(rgb, 18, 0, 0, 0);
    assert_pixel(rgb, 73, 0, 0, 0);
    assert_pixel(rgb, 5, 0, 80, 0);
    memset(rgb, 7, sizeof(rgb));
    gmk_controls_overlay(rgb, GMK_LIGHT_PIXELS, 0, true, false);
    assert_pixel(rgb, 18, 7, 7, 7);
    gmk_controls_overlay(rgb, GMK_LIGHT_PIXELS, 0, false, true);
    assert_pixel(rgb, 18, 7, 7, 7);
    gmk_controls_radio_state(2, GMK_RADIO_CONNECTED);
    memset(rgb, 0, sizeof(rgb));
    gmk_controls_overlay(rgb, GMK_LIGHT_PIXELS, 0, true, true);
    assert_pixel(rgb, 18, 0, 0, 0);
    gmk_controls_radio_leds(2);
    gmk_controls_overlay(rgb, GMK_LIGHT_PIXELS, 0, true, true);
    assert_pixel(rgb, 18, 80, 80, 80);
    gmk_controls_radio_state(2, GMK_RADIO_DISCONNECTED);
    memset(rgb, 0, sizeof(rgb));
    gmk_controls_overlay(rgb, GMK_LIGHT_PIXELS, 0, true, true);
    assert_pixel(rgb, 18, 0, 0, 0);
    gmk_controls_radio_state(2, GMK_RADIO_CONNECTED);
    gmk_controls_overlay(rgb, GMK_LIGHT_PIXELS, 0, true, true);
    assert_pixel(rgb, 18, 0, 0, 0);
    gmk_controls_radio_state(3, GMK_RADIO_CONNECTED);
    memset(rgb, 0, sizeof(rgb));
    gmk_controls_overlay(rgb, GMK_LIGHT_PIXELS, 0, true, true);
    assert_pixel(rgb, 18, 0, 0, 0);
    gmk_controls_radio_state(0, GMK_RADIO_UNKNOWN);
    gmk_controls_overlay(rgb, GMK_LIGHT_PIXELS, 0, true, true);
    assert_pixel(rgb, 18, 80, 80, 80);
}
static void test_stock_slot_pairing_and_os_feedback(void) {
    reset_fixture();
    uint8_t rgb[3 * GMK_LIGHT_PIXELS] = {0};
    gmk_controls_radio_state(1, GMK_RADIO_PAIRING);
    assert(gmk_controls_bt_slot() == 1);
    assert(gmk_controls_visual_delay(0) == 200);
    assert(gmk_controls_visual_delay(199) == 1);
    assert(gmk_controls_visual_delay(200) == 200);
    uint32_t token = gmk_controls_visual_token(0);
    gmk_controls_overlay(rgb, GMK_LIGHT_PIXELS, 0, true, true);
    assert_pixel(rgb, 14, 0, 0, 80);
    gmk_controls_overlay(rgb, GMK_LIGHT_PIXELS, 200, true, true);
    assert_pixel(rgb, 14, 0, 0, 0);
    assert(gmk_controls_visual_token(200) != token);
    gmk_controls_radio_state(6, GMK_RADIO_CONNECTED);
    assert(gmk_controls_visual_delay(0) == 500);
    assert(gmk_controls_visual_delay(999) == 1);
    assert(gmk_controls_visual_delay(1000) == UINT32_MAX);
    gmk_controls_overlay(rgb, GMK_LIGHT_PIXELS, 0, true, true);
    assert_pixel(rgb, 11, 0, 80, 0);
    assert(gmk_controls_mac_set(true) == 0);
    gmk_controls_overlay(rgb, GMK_LIGHT_PIXELS, 0, true, true);
    assert_pixel(rgb, 20, 80, 80, 80);
    assert(gmk_controls_visual_delay(1249) == 1);
    assert(gmk_controls_visual_delay(1250) == 250);
    assert(gmk_controls_visual_delay(1500) == UINT32_MAX);
    token = gmk_controls_visual_token(1250);
    assert(gmk_controls_visual_token(1500) != token);
    memset(rgb, 9, sizeof(rgb));
    gmk_controls_overlay(rgb, GMK_LIGHT_PIXELS, 1500, true, true);
    assert_pixel(rgb, 20, 9, 9, 9);
    assert(gmk_controls_mac_set(false) == 0);
    gmk_controls_overlay(rgb, 36, 0, true, true);
    assert_pixel(rgb, 19, 80, 80, 80);
}
static void test_right_role_is_inert(void) {
    reset_fixture();
    left = false;
    assert(gmk_controls_gui_lock_set(true) == -ENODEV);
    assert(gmk_controls_mac_set(true) == -ENODEV);
    assert(gmk_controls_bt_slot_set(3) == -ENODEV);
    assert(gmk_controls_sleep_selector_set(2) == -ENODEV);
    assert(gmk_controls_usb_sleep_set(true) == -ENODEV);
    gmk_controls_radio_state(2, GMK_RADIO_PAIRING);
    gmk_controls_radio_leds(2);
    assert(!gmk_controls_dirty() && gmk_controls_bt_slot() == 1 && layers == 1);
    radio_test_time = 500;
    gmk_controls_poll(true);
    assert(!preferences.work.pending && save_count == 0);
    assert(gmk_controls_visual_token(0) == 0);
}
int main(void) {
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
    test_preferences_validate_and_restore();
    test_shared_worker_retry_and_concurrent_change();
    test_loaded_bluetooth_slot_survives_early_radio_poll();
#else
    test_no_backend_retains_unsaved_preferences();
#endif
    test_debounce_spans_uptime_rollover();
    test_projection_preserves_native_held_gui();
    test_native_layer_changes_preserve_other_layers();
    test_native_indicators_and_overlay_precedence();
    test_stock_slot_pairing_and_os_feedback();
    test_right_role_is_inert();
    puts("ZMK persistent controls, native HID and indicator tests passed");
    return 0;
}
