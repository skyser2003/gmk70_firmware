#pragma once
#include "../../radio_endpoint_stubs/zmk/hid.h"
#include <zmk/keys.h>
#define ZMK_HID_REPORT_ID_KEYBOARD 1
#define ZMK_HID_REPORT_ID_CONSUMER 2
#define ZMK_HID_KEYBOARD_NKRO_MAX_USAGE HID_USAGE_KEY_KEYBOARD_LANG8
#define ZMK_HID_CONSUMER_MAX_USAGE 0xFFF
zmk_mod_flags_t zmk_hid_get_explicit_mods(void);
int             zmk_hid_register_mods(zmk_mod_flags_t modifiers);
int             zmk_hid_unregister_mods(zmk_mod_flags_t modifiers);
int             zmk_hid_implicit_modifiers_press(zmk_mod_flags_t modifiers);
int             zmk_hid_implicit_modifiers_release(void);
int             zmk_hid_press(uint32_t usage);
int             zmk_hid_release(uint32_t usage);
bool            zmk_hid_is_pressed(uint32_t usage);
int             zmk_hid_masked_modifiers_set(zmk_mod_flags_t modifiers);
int             zmk_hid_masked_modifiers_clear(void);
int             zmk_hid_register_mod(zmk_mod_t modifier);
int             zmk_hid_unregister_mod(zmk_mod_t modifier);
