/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <string.h>
#include <zmk/hid.h>

/* HID clears must remain a different object from endpoint switch callers. */
void zmk_hid_keyboard_clear(void) {
    memset(&zmk_hid_get_keyboard_report()->body, 0, sizeof(struct zmk_hid_keyboard_report_body));
}
void zmk_hid_consumer_clear(void) {
    memset(&zmk_hid_get_consumer_report()->body, 0, sizeof(struct zmk_hid_consumer_report_body));
}
