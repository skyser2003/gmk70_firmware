/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "radio_endpoint_fixture.h"
#include <radio.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>

struct recorded_report {
    bool     consumer;
    uint8_t  keyboard[8];
    uint8_t  overflow[14];
    uint16_t usage;
};
static struct recorded_report reports[16];
static unsigned               report_count;
static bool                   selected;
static int                    submit_result;
static bool                   nkro_enabled = true;

bool gmk_nkro_enabled(void) {
    return nkro_enabled;
}

bool gmk_radio_selected(void) {
    return selected;
}
int gmk_radio_submit_keyboard(const uint8_t report[8]) {
    assert(report_count < 16);
    struct recorded_report *entry = &reports[report_count++];
    entry->consumer               = false;
    memcpy(entry->keyboard, report, 8);
    return submit_result;
}
int gmk_radio_submit_nkro(const uint8_t report[8], const uint8_t overflow[14]) {
    int result = gmk_radio_submit_keyboard(report);
    memcpy(reports[report_count - 1].overflow, overflow, 14);
    return result;
}
int gmk_radio_submit_consumer(uint16_t usage) {
    assert(report_count < 16);
    reports[report_count++] = (struct recorded_report){.consumer = true, .usage = usage};
    return submit_result;
}
void gmk_radio_release(bool consumer) {
    if (consumer) {
        const struct zmk_hid_consumer_report_body zero = {0};
        assert(memcmp(&zmk_hid_get_consumer_report()->body, &zero, sizeof(zero)) == 0);
        gmk_radio_submit_consumer(0);
    } else {
        const struct zmk_hid_keyboard_report_body zero = {0};
        assert(memcmp(&zmk_hid_get_keyboard_report()->body, &zero, sizeof(zero)) == 0);
        const uint8_t release[8] = {0};
        gmk_radio_submit_keyboard(release);
    }
}
static void reset(bool radio) {
    endpoint_fixture_reset();
    memset(reports, 0, sizeof(reports));
    report_count  = 0;
    selected      = radio;
    submit_result = 0;
    nkro_enabled  = true;
}
static void usb_forwarding_preserves_results(void) {
    reset(false);
    endpoint_fixture_set_result(-EAGAIN);
    assert(zmk_endpoint_send_report(HID_USAGE_KEY) == -EAGAIN);
    endpoint_fixture_set_result(17);
    assert(zmk_endpoint_send_report(HID_USAGE_CONSUMER) == 17);
    assert(endpoint_fixture_native_calls() == 2 && report_count == 0);
    assert(zmk_endpoint_send_report(HID_USAGE_GD) == -ENOTSUP);
}
static void keyboard_fast_tap_is_snapshotted_before_release(void) {
    reset(true);
    struct zmk_hid_keyboard_report *keyboard = zmk_hid_get_keyboard_report();
    keyboard->report_id                      = 0x91;
    keyboard->body                           = (struct zmk_hid_keyboard_report_body){.modifiers = 0x42, ._reserved = 0xff};
#if defined(CONFIG_ZMK_HID_REPORT_TYPE_NKRO) && CONFIG_ZMK_HID_REPORT_TYPE_NKRO == 1
    keyboard->body.keys[0] = 0xf0;
    keyboard->body.keys[1] = 3;
#else
    const uint8_t keys[6] = {4, 5, 6, 7, 8, 9};
    memcpy(keyboard->body.keys, keys, sizeof(keys));
#endif
    assert(zmk_endpoint_send_report(HID_USAGE_KEY) == 0);
    keyboard->body = (struct zmk_hid_keyboard_report_body){0};
    assert(zmk_endpoint_send_report(HID_USAGE_KEY) == 0);
    const uint8_t pressed[8]  = {0x42, 0, 4, 5, 6, 7, 8, 9};
    const uint8_t released[8] = {0};
    assert(report_count == 2 && endpoint_fixture_native_calls() == 0);
    assert(!reports[0].consumer && !reports[1].consumer);
    assert(memcmp(reports[0].keyboard, pressed, sizeof(pressed)) == 0);
    assert(memcmp(reports[1].keyboard, released, sizeof(released)) == 0);
    submit_result = -ENOBUFS;
    assert(zmk_endpoint_send_report(HID_USAGE_KEY) == -ENOBUFS);
}
#if defined(CONFIG_ZMK_HID_REPORT_TYPE_NKRO) && CONFIG_ZMK_HID_REPORT_TYPE_NKRO == 1
static void nkro_overflow_is_snapshotted_and_disabled_in_compatibility_mode(void) {
    reset(true);
    gmk_radio_clear_hid();
    report_count                              = 0;
    struct zmk_hid_keyboard_report_body *body = &zmk_hid_get_keyboard_report()->body;
    body->modifiers                           = 2;
    body->keys[0]                             = 0xf0;
    body->keys[1]                             = 0x0f;
    assert(zmk_endpoint_send_report(HID_USAGE_KEY) == 0);
    const uint8_t keyboard[8]  = {2, 0, 4, 5, 6, 7, 8, 9};
    const uint8_t overflow[14] = {0, 0x0c};
    assert(report_count == 1);
    assert(memcmp(reports[0].keyboard, keyboard, sizeof(keyboard)) == 0);
    assert(memcmp(reports[0].overflow, overflow, sizeof(overflow)) == 0);
    nkro_enabled = false;
    assert(zmk_endpoint_send_report(HID_USAGE_KEY) == 0);
    const uint8_t zero[14] = {0};
    assert(memcmp(reports[1].keyboard, keyboard, sizeof(keyboard)) == 0);
    assert(memcmp(reports[1].overflow, zero, sizeof(zero)) == 0);
    nkro_enabled         = true;
    body->keys[0]        = 0x20;
    body->keys[1]        = 2;
    body->keys[0x73 / 8] = 8;
    assert(zmk_endpoint_send_report(HID_USAGE_KEY) == 0);
    const uint8_t holes[8] = {2, 0, 0x73, 5, 0, 0, 0, 9};
    assert(memcmp(reports[2].keyboard, holes, sizeof(holes)) == 0);
    assert(memcmp(reports[2].overflow, zero, sizeof(zero)) == 0);
}
#endif
static void consumer_selects_first_held_usage_and_restores_next(void) {
    reset(true);
    struct zmk_hid_consumer_report *consumer = zmk_hid_get_consumer_report();
    consumer->report_id                      = 0x92;
    consumer->body                           = (struct zmk_hid_consumer_report_body){.keys = {0, 0x123, 0x234, 0, 0, 0x345}};
    assert(zmk_endpoint_send_report(HID_USAGE_CONSUMER) == 0);
    consumer->body.keys[1] = 0;
    assert(zmk_endpoint_send_report(HID_USAGE_CONSUMER) == 0);
    consumer->body = (struct zmk_hid_consumer_report_body){0};
    assert(zmk_endpoint_send_report(HID_USAGE_CONSUMER) == 0);
    assert(report_count == 3 && endpoint_fixture_native_calls() == 0);
    assert(reports[0].consumer && reports[0].usage == 0x123);
    assert(reports[1].consumer && reports[1].usage == 0x234);
    assert(reports[2].consumer && reports[2].usage == 0);
    assert(zmk_endpoint_send_report(HID_USAGE_GD) == -ENOTSUP);
    assert(report_count == 3);
    submit_result = -ENOBUFS;
    assert(zmk_endpoint_send_report(HID_USAGE_CONSUMER) == -ENOBUFS);
}
static void internal_endpoint_clear_releases_radio_through_hid_wrappers(void) {
    reset(true);
    zmk_hid_get_keyboard_report()->body.modifiers = 3;
    zmk_hid_get_keyboard_report()->body.keys[0]   = 4;
    zmk_hid_get_consumer_report()->body.keys[0]   = 0x234;
    endpoint_fixture_internal_clear();
    const uint8_t zero[8] = {0};
    assert(report_count == 2 && endpoint_fixture_native_calls() == 2);
    assert(!reports[0].consumer && memcmp(reports[0].keyboard, zero, sizeof(zero)) == 0);
    assert(reports[1].consumer && reports[1].usage == 0);
    gmk_radio_clear_hid();
    assert(report_count == 4 && endpoint_fixture_native_calls() == 2);
}
static void radio_profile_clear_does_not_send_to_usb(void) {
    reset(true);
    zmk_hid_get_keyboard_report()->body.modifiers = 3;
    zmk_hid_get_keyboard_report()->body.keys[0]   = 4;
    zmk_hid_get_consumer_report()->body.keys[0]   = 0x234;
    /* Native USB can have a pending transfer or a sleeping host while the
     * radio carries typing. It must not delay a radio-to-radio clear. */
    endpoint_fixture_set_result(-EAGAIN);
    gmk_radio_clear_hid();
    const uint8_t zero[8] = {0};
    assert(endpoint_fixture_native_calls() == 0 && report_count == 2);
    assert(!reports[0].consumer && memcmp(reports[0].keyboard, zero, sizeof(zero)) == 0);
    assert(reports[1].consumer && reports[1].usage == 0);
}
static void usb_clear_keeps_native_dispatch_and_releases_radio(void) {
    reset(false);
    zmk_hid_get_keyboard_report()->body.keys[0] = 7;
    zmk_hid_get_consumer_report()->body.keys[0] = 0x123;
    gmk_radio_clear_hid();
    const uint8_t zero[8] = {0};
    assert(endpoint_fixture_native_calls() == 2 && report_count == 2);
    assert(!reports[0].consumer && memcmp(reports[0].keyboard, zero, sizeof(zero)) == 0);
    assert(reports[1].consumer && reports[1].usage == 0);
}
int main(void) {
#if defined(CONFIG_ZMK_HID_REPORT_TYPE_NKRO) && CONFIG_ZMK_HID_REPORT_TYPE_NKRO == 1
    nkro_overflow_is_snapshotted_and_disabled_in_compatibility_mode();
#endif
    usb_forwarding_preserves_results();
    keyboard_fast_tap_is_snapshotted_before_release();
    consumer_selects_first_held_usage_and_restores_next();
    internal_endpoint_clear_releases_radio_through_hid_wrappers();
    radio_profile_clear_does_not_send_to_usb();
    usb_clear_keeps_native_dispatch_and_releases_radio();
    puts("Radio endpoint: native USB, immutable fast taps, consumer arbitration and internal clear boundaries passed");
    return 0;
}
