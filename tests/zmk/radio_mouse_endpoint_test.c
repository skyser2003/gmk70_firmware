/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../firmware/zmk/radio/radio.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>

static struct gmk_radio_core radio;
static bool                  selected;
unsigned                     radio_mouse_native_calls;
unsigned                     function_flip_native_errors;
void                         function_flip_native_debug(const char *format) {
    (void)format;
}
bool gmk_radio_selected(void) {
    return selected;
}
int gmk_radio_submit_keyboard(const uint8_t report[8]) {
    return gmk_radio_core_report(&radio, false, report);
}
int gmk_radio_submit_consumer(uint16_t usage) {
    const uint8_t report[8] = {(uint8_t)usage, (uint8_t)(usage >> 8)};
    return gmk_radio_core_report(&radio, true, report);
}
int gmk_radio_submit_mouse(const uint8_t report[5]) {
    return gmk_radio_core_mouse(&radio, report);
}
void gmk_radio_release(bool consumer) {
    gmk_radio_core_release(&radio, consumer);
}
void gmk_radio_release_mouse(void) {
    gmk_radio_core_release_mouse(&radio);
}
static void drain(void) {
    struct gmk_radio_frame frame;
    for (unsigned n = 0; n < 10 && gmk_radio_core_next(&radio, 0, &frame, false); ++n) {
        gmk_radio_core_sent(&radio, 0, true);
        gmk_radio_core_ack(&radio);
    }
}
static void expect(const uint8_t *bytes, uint8_t length) {
    struct gmk_radio_frame frame;
    assert(gmk_radio_core_next(&radio, 0, &frame, false));
    assert(frame.length == length && !memcmp(frame.bytes, bytes, length));
    gmk_radio_core_sent(&radio, 0, true);
}
int main(void) {
    assert(zmk_endpoint_send_mouse_report() == -EAGAIN && radio_mouse_native_calls == 1);
    gmk_radio_core_init(&radio);
    assert(gmk_radio_core_select(&radio, GMK_HOST_BT1, false) == 0);
    drain();
    gmk_radio_core_notification(&radio, 0x5b, 0x32);
    drain();
    selected = true;
    assert(zmk_hid_mouse_button_press(0) == 0);
    zmk_hid_mouse_movement_set(INT16_MIN, INT16_MAX);
    zmk_hid_mouse_scroll_set(-1000, 1000);
    assert(zmk_endpoint_send_mouse_report() == 0 && radio_mouse_native_calls == 1);
    assert(zmk_hid_mouse_button_release(0) == 0);
    zmk_hid_mouse_movement_set(0, 0);
    zmk_hid_mouse_scroll_set(0, 0);
    assert(zmk_endpoint_send_mouse_report() == 0);
    const uint8_t pressed[] = {0xa8, 1, 0x81, 0x7f, 0x7f, 0x81, 0xa9};
    const uint8_t zero[]    = {0xa8, 0, 0, 0, 0, 0, 0xa8};
    expect(pressed, sizeof(pressed));
    expect(zero, sizeof(zero));
    assert(zmk_hid_mouse_button_press(0) == 0);
    assert(zmk_endpoint_send_mouse_report() == 0);
    expect((const uint8_t[]){0xa8, 1, 0, 0, 0, 0, 0xa9}, 7);
    zmk_hid_mouse_clear();
    const struct zmk_hid_mouse_report_body cleared = {0};
    assert(!memcmp(&zmk_hid_get_mouse_report()->body, &cleared, sizeof(cleared)));
    expect(zero, sizeof(zero));
    assert(zmk_hid_mouse_button_release(0) == 0);
    assert(zmk_hid_mouse_button_press(0) == 0);
    zmk_hid_mouse_movement_set(10, -10);
    gmk_radio_clear_hid();
    assert(!memcmp(&zmk_hid_get_mouse_report()->body, &cleared, sizeof(cleared)));
    expect((const uint8_t[]){0xa1, 0, 0, 0, 0, 0, 0, 0, 0, 0xa1}, 10);
    expect((const uint8_t[]){0xa2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xa2}, 16);
    expect((const uint8_t[]){0xa3, 0, 0, 0xa3}, 4);
    expect(zero, sizeof(zero));
    assert(!radio.count && !radio.release && radio_mouse_native_calls == 1);
    zmk_hid_get_keyboard_report()->body.modifiers = 4;
    zmk_hid_get_keyboard_report()->body.keys[0]   = 4;
    zmk_hid_get_consumer_report()->body.keys[0]   = 0xcd;
    assert(!zmk_hid_mouse_button_press(0));
    zmk_hid_mouse_movement_set(10, -10);
    gmk_radio_resume_hid();
    expect((const uint8_t[]){0xa1, 4, 0, 4, 0, 0, 0, 0, 0, 0xa9}, 10);
    expect((const uint8_t[]){0xa3, 0xcd, 0, 0x70}, 4);
    expect((const uint8_t[]){0xa8, 1, 0, 0, 0, 0, 0xa9}, 7);
    assert(zmk_hid_get_keyboard_report()->body.modifiers == 4);
    gmk_radio_core_notification(&radio, 0x5b, 0x33);
    assert(zmk_endpoint_send_mouse_report() == -ENOTCONN);
    assert(!function_flip_native_errors);
    puts("Radio mouse endpoint: pinned native HID, immutable stock projection, saturation and clear passed");
    return 0;
}
