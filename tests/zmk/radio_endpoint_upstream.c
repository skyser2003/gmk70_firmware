/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "radio_endpoint_fixture.h"
#include <errno.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>

static struct zmk_hid_keyboard_report keyboard;
static struct zmk_hid_consumer_report consumer;
static unsigned                       native_calls;
static int                            native_result;

struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void) {
    return &keyboard;
}
struct zmk_hid_consumer_report *zmk_hid_get_consumer_report(void) {
    return &consumer;
}
int zmk_endpoint_send_report(uint16_t usage_page) {
    ++native_calls;
    return usage_page == HID_USAGE_KEY || usage_page == HID_USAGE_CONSUMER ? native_result : -ENOTSUP;
}

/* Keep these same-object sends: upstream internal dispatch bypasses --wrap. */
void zmk_endpoint_clear_reports(void) {
    zmk_hid_keyboard_clear();
    zmk_hid_consumer_clear();
    zmk_endpoint_send_report(HID_USAGE_KEY);
    zmk_endpoint_send_report(HID_USAGE_CONSUMER);
}
void endpoint_fixture_internal_clear(void) {
    zmk_endpoint_clear_reports();
}
void endpoint_fixture_reset(void) {
    keyboard      = (struct zmk_hid_keyboard_report){0};
    consumer      = (struct zmk_hid_consumer_report){0};
    native_calls  = 0;
    native_result = 0;
}
unsigned endpoint_fixture_native_calls(void) {
    return native_calls;
}
void endpoint_fixture_set_result(int result) {
    native_result = result;
}
