/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "radio.h"
#include <errno.h>
#include <string.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>

#if !defined(CONFIG_ZMK_HID_REPORT_TYPE_HKRO) || CONFIG_ZMK_HID_REPORT_TYPE_HKRO != 1 || CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE != 6
#    error "GMK70 radio endpoint requires the native HKRO6 keyboard report"
#endif

int  __real_zmk_endpoint_send_report(uint16_t usage_page);
void __real_zmk_hid_keyboard_clear(void);
void __real_zmk_hid_consumer_clear(void);

int __wrap_zmk_endpoint_send_report(uint16_t usage_page) {
    if (!gmk_radio_selected()) return __real_zmk_endpoint_send_report(usage_page);
    switch (usage_page) {
        case HID_USAGE_KEY: {
            const struct zmk_hid_keyboard_report_body *body      = &zmk_hid_get_keyboard_report()->body;
            uint8_t                                    report[8] = {body->modifiers, 0};
            memcpy(report + 2, body->keys, sizeof(body->keys));
            return gmk_radio_submit_keyboard(report);
        }
        case HID_USAGE_CONSUMER: {
            const struct zmk_hid_consumer_report_body *body  = &zmk_hid_get_consumer_report()->body;
            uint16_t                                   usage = 0;
            for (unsigned i = 0; i < CONFIG_ZMK_HID_CONSUMER_REPORT_SIZE; ++i) {
                if (body->keys[i]) {
                    usage = body->keys[i];
                    break;
                }
            }
            return gmk_radio_submit_consumer(usage);
        }
        default:
            return -ENOTSUP;
    }
}
void __wrap_zmk_hid_keyboard_clear(void) {
    __real_zmk_hid_keyboard_clear();
    gmk_radio_release(false);
}
void __wrap_zmk_hid_consumer_clear(void) {
    __real_zmk_hid_consumer_clear();
    gmk_radio_release(true);
}
void gmk_radio_clear_hid(void) {
    zmk_endpoint_clear_reports();
}
