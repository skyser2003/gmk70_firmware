/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <errno.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
extern unsigned radio_mouse_native_calls;
int             zmk_endpoint_send_report(uint16_t usage_page) {
    (void)usage_page;
    return 0;
}
int zmk_endpoint_send_mouse_report(void) {
    ++radio_mouse_native_calls;
    return -EAGAIN;
}
void zmk_endpoint_clear_reports(void) {
    zmk_hid_keyboard_clear();
    zmk_hid_consumer_clear();
    zmk_hid_mouse_clear();
}
