/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "radio.h"
#include "radio_keys.h"
#include <errno.h>
#include <string.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#ifdef CONFIG_GMK70_FUNCTION_FLIP
#    include "../function_flip/flip.h"
#endif

#if defined(CONFIG_ZMK_HID_REPORT_TYPE_NKRO) && CONFIG_ZMK_HID_REPORT_TYPE_NKRO == 1
#    include "nkro.h"
static struct gmk70_radio_keys keys;
#elif !defined(CONFIG_ZMK_HID_REPORT_TYPE_HKRO) || CONFIG_ZMK_HID_REPORT_TYPE_HKRO != 1 || CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE != 6
#    error "GMK70 radio endpoint requires native NKRO or HKRO6 keyboard reports"
#endif

int  __real_zmk_endpoint_send_report(uint16_t usage_page);
void __real_zmk_hid_keyboard_clear(void);
void __real_zmk_hid_consumer_clear(void);
#if defined(CONFIG_ZMK_POINTING) && CONFIG_ZMK_POINTING
int            __real_zmk_endpoint_send_mouse_report(void);
void           __real_zmk_hid_mouse_clear(void);
static uint8_t mouse_axis(int16_t value) {
    /* The stock module uses signed eight-bit relative axes; native ZMK keeps
     * sixteen-bit values. Saturate instead of wrapping a large native delta. */
    int16_t bounded = value < -127 ? -127 : value > 127 ? 127 : value;
    return (uint8_t)(int8_t)bounded;
}
int __wrap_zmk_endpoint_send_mouse_report(void) {
    if (!gmk_radio_selected()) return __real_zmk_endpoint_send_mouse_report();
    const struct zmk_hid_mouse_report_body *body      = &zmk_hid_get_mouse_report()->body;
    const uint8_t                           report[5] = {body->buttons, mouse_axis(body->d_x), mouse_axis(body->d_y), mouse_axis(body->d_scroll_y), mouse_axis(body->d_scroll_x)};
    return gmk_radio_submit_mouse(report);
}
void __wrap_zmk_hid_mouse_clear(void) {
    __real_zmk_hid_mouse_clear();
    gmk_radio_release_mouse();
}
#endif

__attribute__((noipa)) int __wrap_zmk_endpoint_send_report(uint16_t usage_page) {
    if (!gmk_radio_selected()) return __real_zmk_endpoint_send_report(usage_page);
    switch (usage_page) {
        case HID_USAGE_KEY: {
            const struct zmk_hid_keyboard_report_body *body      = &zmk_hid_get_keyboard_report()->body;
            uint8_t                                    report[8] = {body->modifiers, 0};
#if defined(CONFIG_ZMK_HID_REPORT_TYPE_NKRO) && CONFIG_ZMK_HID_REPORT_TYPE_NKRO == 1
            uint8_t overflow[14];
            gmk70_radio_keys_encode(&keys, body->modifiers, body->keys, sizeof(body->keys), report, overflow);
            if (!gmk_nkro_enabled()) memset(overflow, 0, sizeof(overflow));
            return gmk_radio_submit_nkro(report, overflow);
#else
            memcpy(report + 2, body->keys, sizeof(body->keys));
            return gmk_radio_submit_keyboard(report);
#endif
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
#ifdef CONFIG_GMK70_FUNCTION_FLIP
    gmk_flip_clear_begin();
#endif
    __real_zmk_hid_keyboard_clear();
#ifdef CONFIG_GMK70_FUNCTION_FLIP
    gmk_flip_clear(HID_USAGE_KEY);
#endif
#if defined(CONFIG_ZMK_HID_REPORT_TYPE_NKRO) && CONFIG_ZMK_HID_REPORT_TYPE_NKRO == 1
    memset(&keys, 0, sizeof(keys));
#endif
    gmk_radio_release(false);
#ifdef CONFIG_GMK70_FUNCTION_FLIP
    gmk_flip_clear_end();
#endif
}
void __wrap_zmk_hid_consumer_clear(void) {
#ifdef CONFIG_GMK70_FUNCTION_FLIP
    gmk_flip_clear_begin();
#endif
    __real_zmk_hid_consumer_clear();
#ifdef CONFIG_GMK70_FUNCTION_FLIP
    gmk_flip_clear(HID_USAGE_CONSUMER);
#endif
    gmk_radio_release(true);
#ifdef CONFIG_GMK70_FUNCTION_FLIP
    gmk_flip_clear_end();
#endif
}
void gmk_radio_clear_hid(void) {
    if (!gmk_radio_selected()) {
        zmk_endpoint_clear_reports();
        return;
    }
    /* Upstream clear sends from the same translation unit, bypassing the
     * endpoint wrapper. While radio owns typing, clear through its reserved
     * release barriers without waiting for unrelated native USB transfers. */
    __wrap_zmk_hid_keyboard_clear();
    __wrap_zmk_hid_consumer_clear();
#if defined(CONFIG_ZMK_POINTING) && CONFIG_ZMK_POINTING
    __wrap_zmk_hid_mouse_clear();
#endif
}
void gmk_radio_resume_hid(void) {
    /* A key can cancel pre-STOP after the radio received USB selection.
     * Requeue native held state without clearing it or replaying mouse deltas. */
    (void)__wrap_zmk_endpoint_send_report(HID_USAGE_KEY);
    (void)__wrap_zmk_endpoint_send_report(HID_USAGE_CONSUMER);
#if defined(CONFIG_ZMK_POINTING) && CONFIG_ZMK_POINTING
    const uint8_t buttons[5] = {zmk_hid_get_mouse_report()->body.buttons};
    (void)gmk_radio_submit_mouse(buttons);
#endif
}
