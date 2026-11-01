/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise unchanged pinned HID and listener through the board adapters. */
#include <assert.h>
#include <stdio.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#include "../../dependencies/zmk/app/src/hid_listener.c"
#include "../../firmware/zmk/function_flip/flip.c"
#include "../../firmware/zmk/nkro/nkro.h"
#include "../../firmware/zmk/radio/radio.h"

uint32_t               radio_test_time;
_Thread_local unsigned radio_test_locked;
unsigned               function_flip_native_errors;
static unsigned        canceled_press_logs;
void                   function_flip_native_debug(const char *format) {
    if (strcmp(format, "Unable to press keycode") == 0 || strcmp(format, "Unable to pre-release keycode (%d)") == 0) ++canceled_press_logs;
}
const struct zmk_event_type zmk_event_zmk_keycode_state_changed = {.name = "keycode-state-changed"};
static bool                 mac                                 = true;
static int                  brightness;
struct snapshot {
    uint16_t                       page;
    struct zmk_hid_keyboard_report keyboard;
    zmk_hid_boot_report_t          boot;
    struct zmk_hid_consumer_report consumer;
};
static struct snapshot reports[1024];
static unsigned        report_count;
static bool            nested, nested_custom, foreign;
enum clear_injection { CLEAR_NONE, CLEAR_BEFORE_PRESS, CLEAR_AFTER_EXPLICIT, CLEAR_FROM_THREAD };
static enum clear_injection clear_injection;
static bool                 nested_after_clear;
static bool                 clear_before_release;
static atomic_bool          threaded_clear_started;
static pthread_t            clear_thread;
static void                *concurrent_clear(void *context) {
    assert(context == NULL);
    assert(pthread_mutex_trylock(&dispatch_mutex.native) == EBUSY);
    atomic_store(&threaded_clear_started, true);
    zmk_endpoint_clear_reports();
    return NULL;
}
static void source_payload(uint32_t encoded, bool pressed, uint8_t explicit_modifiers);
static void inject_clear(void) {
    clear_injection = CLEAR_NONE;
    zmk_endpoint_clear_reports();
    if (nested_after_clear) {
        nested_after_clear = false;
        source_payload(0x0107002b, true, 2);
    }
}
bool __real_zmk_hid_is_pressed(uint32_t usage);
int  __real_zmk_hid_register_mods(uint8_t modifiers);
bool __wrap_zmk_hid_is_pressed(uint32_t usage) {
    bool pressed = __real_zmk_hid_is_pressed(usage);
    if (clear_injection == CLEAR_FROM_THREAD) {
        clear_injection = CLEAR_NONE;
        atomic_store(&threaded_clear_started, false);
        assert(pthread_create(&clear_thread, NULL, concurrent_clear, NULL) == 0);
        while (!atomic_load(&threaded_clear_started)) {
        }
    }
    if (clear_injection == CLEAR_BEFORE_PRESS) {
        inject_clear();
    }
    return pressed;
}
int __wrap_zmk_hid_register_mods(uint8_t modifiers) {
    int result = __real_zmk_hid_register_mods(modifiers);
    if (clear_injection == CLEAR_AFTER_EXPLICIT) {
        inject_clear();
    }
    return result;
}
int  function_flip_native_nkro_init(void);
bool gmk_wired_is_left(void) {
    return true;
}
bool zmk_keymap_layer_active(uint8_t layer) {
    return mac && layer == 2;
}
int zmk_rgb_underglow_change_brt(int direction) {
    brightness += direction;
    return 0;
}
struct zmk_keycode_state_changed *as_zmk_keycode_state_changed(const zmk_event_t *event) {
    assert(event->event == &zmk_event_zmk_keycode_state_changed);
    return &((struct zmk_keycode_state_changed_event *)event)->data;
}
static void source_payload(uint32_t encoded, bool pressed, uint8_t explicit_modifiers) {
    struct zmk_keycode_state_changed event = zmk_keycode_state_changed_from_encoded(encoded, pressed, 100);
    event.explicit_modifiers               = explicit_modifiers;
    assert(__wrap_raise_zmk_keycode_state_changed(event) == 0);
}
static void source(uint32_t encoded, bool pressed) {
    source_payload(encoded, pressed, 0);
}
static void *foreign_implicit(void *context) {
    assert(context == NULL);
    assert(zmk_hid_implicit_modifiers_press(0x01) >= 0);
    return NULL;
}
int __real_raise_zmk_keycode_state_changed(struct zmk_keycode_state_changed event) {
    assert(!radio_test_locked);
    if (clear_before_release && !event.state) {
        clear_before_release = false;
        zmk_endpoint_clear_reports();
    }
    if (nested) {
        nested = false;
        source(0x02070005, true);
        source(0x02070005, false);
    }
    if (foreign) {
        foreign = false;
        pthread_t thread;
        assert(pthread_create(&thread, NULL, foreign_implicit, NULL) == 0);
        assert(pthread_join(thread, NULL) == 0);
    }
    if (nested_custom) {
        nested_custom = false;
        source(0x02070021, true);
    }
    struct zmk_keycode_state_changed_event envelope = {.header = {.event = &zmk_event_zmk_keycode_state_changed}, .data = event};
    return hid_listener(&envelope.header);
}
int zmk_endpoint_send_report(uint16_t page) {
    assert(!radio_test_locked && report_count < 1024);
    assert(page == HID_USAGE_KEY || page == HID_USAGE_CONSUMER);
    reports[report_count++] = (struct snapshot){.page = page, .keyboard = *zmk_hid_get_keyboard_report(), .boot = *zmk_hid_get_boot_report(), .consumer = *zmk_hid_get_consumer_report()};
    return 0;
}
int __real_zmk_endpoint_send_report(uint16_t page) {
    return zmk_endpoint_send_report(page);
}
void zmk_endpoint_clear_reports(void) {
    zmk_hid_keyboard_clear();
    zmk_hid_consumer_clear();
    assert(zmk_endpoint_send_report(7) == 0);
    assert(zmk_endpoint_send_report(12) == 0);
}
bool gmk_radio_selected(void) {
    return false;
}
void gmk_radio_release(bool consumer) {
    (void)consumer;
}
int gmk_radio_submit_nkro(const uint8_t report[8], const uint8_t overflow[14]) {
    (void)report;
    (void)overflow;
    assert(false);
    return -EINVAL;
}
int gmk_radio_submit_consumer(uint16_t usage) {
    (void)usage;
    assert(false);
    return -EINVAL;
}
static bool down(uint8_t usage) {
    return (zmk_hid_get_keyboard_report()->body.keys[usage / 8] & (1U << (usage % 8))) != 0;
}
static void mods(uint8_t expected) {
    assert(zmk_hid_get_keyboard_report()->body.modifiers == expected);
    assert(zmk_hid_get_boot_report()->modifiers == expected);
}
static void unrelated_keys_and_both_release_orders(void) {
    for (unsigned order = 0; order < 2; ++order) {
        source(0x70020, true);
        mods(8);
        source(0x70004, true);
        assert(down(0x2b) && down(4));
        mods(8);
        source(0x70004, false);
        mods(8);
        source(0x70021, true);
        mods(8);
        source(order ? 0x70021 : 0x70020, false);
        assert(down(order ? 0x2b : 8));
        mods(8);
        source(order ? 0x70020 : 0x70021, false);
        mods(0);
    }
}
static void explicit_native_implicit_and_overlapping_keys(void) {
    source(0x700e3, true);
    source(0x700e1, true);
    source(0x70020, true);
    source(0x70020, false);
    mods(0x0a);
    source(0x700e3, false);
    mods(2);
    source(0x700e1, false);
    mods(0);
    source(0x01070004, true);
    source(0x02070020, true);
    mods(0x0b);
    source(0x02070020, false);
    mods(1);
    source(0x01070004, false);
    mods(0);
    source(0x70020, true);
    source(0x02070004, true);
    mods(0x0a);
    source(0x02070004, false);
    mods(8);
    source(0x7002b, true); /* Native target overlap follows upstream bitmap semantics. */
    mods(8);
    source(0x7002b, false);
    assert(!down(0x2b));
    mods(8);
    source(0x70020, false);
    mods(0);
    /* Unaffected native shortcuts retain the upstream singleton behavior. */
    source(0x01070004, true);
    source(0x02070005, true);
    mods(2);
    unsigned before = report_count;
    source(0x01070004, false);
    assert(report_count - before == (CONFIG_ZMK_HID_SEPARATE_MOD_RELEASE_REPORT ? 2U : 1U));
    assert(reports[before].keyboard.body.modifiers == (CONFIG_ZMK_HID_SEPARATE_MOD_RELEASE_REPORT ? 2 : 0));
    mods(0);
    source(0x02070005, false);
}
static void consumer_brightness_and_release_reports(void) {
    unsigned before = report_count;
    source(0x0207001e, true); /* Shift plus mapped consumer brightness-down. */
    assert(zmk_hid_is_pressed(0xc0070));
    assert(report_count == before + 2 && reports[before].page == 7 && reports[before].keyboard.body.modifiers == 2);
    mods(2);
    before = report_count;
    source(0x0207001e, false);
    assert(report_count == before + (CONFIG_ZMK_HID_SEPARATE_MOD_RELEASE_REPORT ? 3U : 2U));
    assert(reports[report_count - 2].page == 7 && reports[report_count - 2].keyboard.body.modifiers == 0);
    mods(0);
    before = report_count;
    source(0x04070022, true);
    source(0x04070022, false);
    assert(brightness == -1 && report_count == before);
    mods(0);
    source(0x70020, true);
    before = report_count;
    source(0x70020, false);
    assert(reports[before].keyboard.body.modifiers == (CONFIG_ZMK_HID_SEPARATE_MOD_RELEASE_REPORT ? 8 : 0));
    mods(0);
}
static void duplicates_mode_changes_and_clear(void) {
    source(0x02070020, true);
    source(0x02070020, true);
    mods(0x0a);
    assert(gmk_flip_toggle() == 0);
    mac = false;
    source(0x70020, false);
    source(0x70020, false);
    mods(0);
    assert(!down(0x2b));
    assert(gmk_flip_toggle() == 0);
    source(0x01070020, true); /* Windows 3 -> Ctrl+F3. */
    mac = true;
    source(0x02070020, true); /* Replace it with Mac Shift+GUI+Tab. */
    assert(!down(0x3c) && down(0x2b));
    mods(0x0a);
    source(0x0207001e, true);
    zmk_endpoint_clear_reports();
    mods(0);
    assert(!down(0x2b) && !zmk_hid_is_pressed(0xc0070));
    source(0x70004, true);
    mods(0);
    source(0x70020, false);
    source(0x70020, false);
    source(0x7001e, false);
    mods(0);
    source(0x70004, false);
    source(0x70020, true);
    mods(8);
    source(0x70020, false);
    mods(0);
    source(0x0207001e, true);
    zmk_hid_consumer_clear();
    mods(0);
    source(0x7001e, false);
    mods(0);
}
static unsigned key_count(void) {
    unsigned count = 0;
    for (uint8_t usage = 1; usage < 150; ++usage)
        if (down(usage)) ++count;
    return count;
}
static void projections_and_scope_isolation(void) {
    source(0x70020, true);
    for (uint32_t usage = 4; usage < 11; ++usage)
        source(0x70000 | usage, true);
    assert(key_count() == 8);
    mods(8);
    for (unsigned i = 0; i < 6; ++i)
        assert(zmk_hid_get_boot_report()->keys[i] == HID_ERROR_ROLLOVER);
    assert(gmk_nkro_set(false) == 0 && key_count() == 6);
    mods(8);
    assert(gmk_nkro_set(true) == 0 && key_count() == 8);
    for (uint32_t usage = 4; usage < 11; ++usage)
        source(0x70000 | usage, false);
    source(0x70020, false);
    mods(0);
    nested = true;
    source(0x70020, true);
    mods(8);
    foreign = true;
    source(0x70021, true);
    mods(9); /* Another emitter's native helper must not inherit our exemption. */
    source(0x70020, false);
    source(0x70021, false);
    mods(1);
    assert(zmk_hid_implicit_modifiers_release() >= 0);
    mods(0);
    nested_custom = true;
    source(0x70020, true);
    mods(0x0a);
    source(0x70020, false);
    mods(0x0a);
    source(0x70021, false);
    mods(0);
    assert(dispatch == NULL);
}
static void disabled_flip_keeps_native_semantics(void) {
    assert(gmk_flip_toggle() == 0);
    source(0x01070020, true);
    assert(down(0x20));
    mods(1);
    source(0x70004, true);
    mods(0);
    source(0x70004, false);
    source(0x01070020, false);
    source(0x02070020, true);
    zmk_endpoint_clear_reports();
    source(0x02070020, false);
    mods(0);
    source(0x70004, true);
    mods(0);
    source(0x70004, false);
    assert(gmk_flip_toggle() == 0);
}
static void clear_during_custom_press_cannot_repopulate_reports(void) {
    for (unsigned consumer = 0; consumer < 2; ++consumer) {
        for (enum clear_injection injection = CLEAR_BEFORE_PRESS; injection <= CLEAR_AFTER_EXPLICIT; ++injection) {
            uint32_t encoded = consumer ? 0x0207001e : 0x02070020;
            uint32_t target  = consumer ? 0xc0070 : 0x7002b;
            unsigned before  = report_count;
            clear_injection  = injection;
            source_payload(encoded, true, 0x10);
            assert(!zmk_hid_is_pressed(target));
            mods(0);
            source(encoded, false);
            assert(!zmk_hid_is_pressed(target));
            mods(0);
            for (unsigned i = before; i < report_count; ++i) {
                assert(reports[i].keyboard.body.modifiers == 0);
                assert((reports[i].keyboard.body.keys[0x2b / 8] & (1U << (0x2b % 8))) == 0);
                for (unsigned k = 0; k < 6; ++k)
                    assert(reports[i].consumer.body.keys[k] == 0);
            }
            source(encoded, true);
            assert(zmk_hid_is_pressed(target));
            source(encoded, false);
            assert(!zmk_hid_is_pressed(target));
            mods(0);
        }
    }
}
static void nested_native_ownership_survives_canceled_custom_press(void) {
    for (enum clear_injection injection = CLEAR_BEFORE_PRESS; injection <= CLEAR_AFTER_EXPLICIT; ++injection) {
        for (unsigned preheld = 0; preheld < 2; ++preheld) {
            if (preheld) source(0x7002b, true);
            clear_injection    = injection;
            nested_after_clear = true;
            source_payload(0x02070020, true, 2);
            assert(down(0x2b) && zmk_hid_get_explicit_mods() == 2);
            mods(3);
            source_payload(0x02070020, false, 2);
            assert(down(0x2b));
            mods(3);
            source_payload(0x0107002b, false, 2);
            if (preheld) source(0x7002b, false);
            assert(!down(0x2b) && zmk_hid_get_explicit_mods() == 0);
            mods(0);
        }
    }
    assert(zmk_hid_press(0x10081) == -EINVAL);
    assert(zmk_hid_release(0x10081) == -EINVAL);
}
static void keyboard_clear_preserves_consumer_release_context(void) {
    source(0x0207001e, true);
    assert(zmk_hid_is_pressed(0xc0070));
    mods(2);
    zmk_hid_keyboard_clear();
    assert(zmk_hid_is_pressed(0xc0070));
    mods(0);
    source(0x0207001e, false);
    assert(!zmk_hid_is_pressed(0xc0070));
    mods(0);
}
static void concurrent_clear_serializes_with_custom_press(void) {
    for (unsigned consumer = 0; consumer < 2; ++consumer) {
        uint32_t encoded = consumer ? 0x0207001e : 0x02070020;
        uint32_t target  = consumer ? 0xc0070 : 0x7002b;
        clear_injection  = CLEAR_FROM_THREAD;
        source(encoded, true);
        assert(pthread_join(clear_thread, NULL) == 0);
        assert(!zmk_hid_is_pressed(target));
        mods(0);
        assert(reports[report_count - 2].page == 7 && reports[report_count - 2].keyboard.body.modifiers == 0);
        assert(reports[report_count - 1].page == 12);
        for (unsigned k = 0; k < 6; ++k)
            assert(reports[report_count - 1].consumer.body.keys[k] == 0);
        source(encoded, false);
        assert(!zmk_hid_is_pressed(target));
        mods(0);
    }
}
static void clear_during_duplicate_replacement_cancels_new_press(void) {
    source(0x70020, true);
    unsigned before      = report_count;
    clear_before_release = true;
    source(0x02070020, true);
    assert(!down(0x2b));
    mods(0);
    for (unsigned i = before; i < report_count; ++i) {
        assert((reports[i].keyboard.body.keys[0x2b / 8] & (1U << (0x2b % 8))) == 0);
        assert(reports[i].keyboard.body.modifiers == 0);
    }
    source(0x70020, false);
    assert(!down(0x2b));
}
int main(void) {
    (void)registered_set;
    assert(registered_init() == 0 && function_flip_native_nkro_init() == 0);
    assert(gmk_flip_toggle() == 0);
    unrelated_keys_and_both_release_orders();
    explicit_native_implicit_and_overlapping_keys();
    consumer_brightness_and_release_reports();
    duplicates_mode_changes_and_clear();
    projections_and_scope_isolation();
    disabled_flip_keeps_native_semantics();
    clear_during_custom_press_cannot_repopulate_reports();
    nested_native_ownership_survives_canceled_custom_press();
    keyboard_clear_preserves_consumer_release_context();
    concurrent_clear_serializes_with_custom_press();
    clear_during_duplicate_replacement_cancels_new_press();
    assert(function_flip_native_errors == 0);
    assert(canceled_press_logs == 4);
    printf("Function flip native HID/listener: independent ownership, native semantics, clears, scopes, NKRO/6KRO/boot passed (separate-release=%d, reports=%u)\n", CONFIG_ZMK_HID_SEPARATE_MOD_RELEASE_REPORT, report_count);
    return 0;
}
