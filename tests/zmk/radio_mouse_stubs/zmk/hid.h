/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../../function_flip_native_stubs/zmk/hid.h"
#include <zmk/pointing.h>
enum { ZMK_HID_REPORT_ID_MOUSE = 3, ZMK_HID_MOUSE_NUM_BUTTONS = 5 };
struct zmk_hid_mouse_report_body {
    zmk_mouse_button_flags_t buttons;
    int16_t                  d_x, d_y, d_scroll_y, d_scroll_x;
} __attribute__((packed));
struct zmk_hid_mouse_report {
    uint8_t                          report_id;
    struct zmk_hid_mouse_report_body body;
} __attribute__((packed));
int                          zmk_hid_mouse_button_press(zmk_mouse_button_t button);
int                          zmk_hid_mouse_button_release(zmk_mouse_button_t button);
void                         zmk_hid_mouse_movement_set(int16_t x, int16_t y);
void                         zmk_hid_mouse_scroll_set(int16_t x, int16_t y);
void                         zmk_hid_mouse_clear(void);
struct zmk_hid_mouse_report *zmk_hid_get_mouse_report(void);
