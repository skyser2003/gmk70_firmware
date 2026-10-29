/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>

struct zmk_hid_keyboard_report_body {
    uint8_t modifiers;
    uint8_t _reserved;
    uint8_t keys[6];
};
struct zmk_hid_keyboard_report {
    uint8_t                             report_id;
    struct zmk_hid_keyboard_report_body body;
};
struct zmk_hid_consumer_report_body {
    uint16_t keys[6];
};
struct zmk_hid_consumer_report {
    uint8_t                             report_id;
    struct zmk_hid_consumer_report_body body;
};
struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void);
struct zmk_hid_consumer_report *zmk_hid_get_consumer_report(void);
void                            zmk_hid_keyboard_clear(void);
void                            zmk_hid_consumer_clear(void);
