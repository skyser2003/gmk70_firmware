// Copyright 2024 yangzheng20003 (@yangzheng20003)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "rgb_record/rgb_record.h"

enum layers {
    _BL = 0,
    _FL,
    _MBL,
    _MFL,
};

#define ______ HS_BLACK
#define KC_BTPR ______
#define MM_BATQ ______

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

    [_BL] = LAYOUT( /* Base */
        KC_MUTE,    KC_GRV,   KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_ESC,             KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_MINS,  KC_EQL,   KC_BSPC,
        KC_F1,      KC_TAB,   KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,                         KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC,  KC_BSLS,
        KC_F5,      KC_LCTL,  KC_A,     KC_S,     KC_D,     KC_F,     KC_G,                         KC_H,     KC_J,     KC_K,     KC_L,     KC_SCLN,  KC_QUOT,            KC_ENT,
        KC_F11,     KC_LSFT,  KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,               KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,  KC_RSFT,  KC_UP,    KC_DEL,
        KC_F12,     KC_LGUI,  MO(_FL),  KC_LALT,            KC_SPC,                                 KC_SPC,             KC_RALT,  KC_PSCR,            KC_LEFT,  KC_DOWN,  KC_RGHT),

    [_FL] = LAYOUT( /* Base */
        _______,    EE_CLR,   KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,    QK_BOOT,            KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,   EE_CLR,
        _______,    UG_NEXT,  KC_BT1,   KC_BT2,   KC_BT3,   KC_2G4,   _______,                      KC_PSCR,  KC_SCRL,  KC_PAUSE, _______,  _______,  UG_HUEU,  UG_HUED,  RL_MOD,
        _______,    _______,  KC_NO,    TO(_MBL), _______,  _______,  _______,                      KC_HOME,  KC_END,   KC_PGUP,  KC_PGDN,  UG_SATD,  UG_SATU,            KC_END,
        _______,    _______,  _______,  UG_TOGG,  _______,  NK_TOGG,  HS_BATQ,            HS_BATQ,  _______,  _______,  _______,  _______,  _______,  KC_BTPR,  UG_VALU,  KC_INS,
        _______,    KC_FILP,  _______,  KC_LALT,            KC_BATQ,                                KC_BATQ,            KC_RALT,  GU_TOGG,            UG_SPDU,  UG_VALD,  UG_SPDD),

    [_MBL] = LAYOUT( /* Base */
        KC_MUTE,    KC_GRV,   KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_ESC,             KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_MINS,  KC_EQL,   KC_BSPC,
        KC_F1,      KC_TAB,   KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,                         KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC,  KC_BSLS,
        KC_F5,      KC_LCTL,  KC_A,     KC_S,     KC_D,     KC_F,     KC_G,                         KC_H,     KC_J,     KC_K,     KC_L,     KC_SCLN,  KC_QUOT,            KC_ENT,
        KC_F11,     KC_LSFT,  KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,               KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,  KC_RSFT,  KC_UP,    KC_DEL,
        KC_F12,     KC_LGUI,  MO(_MFL), KC_LGUI,            KC_SPC,                                 KC_SPC,             KC_RGUI,  KC_PSCR,            KC_LEFT,  KC_DOWN,  KC_RGHT),

    [_MFL] = LAYOUT( /* Base */
        _______,    _______,  _______,  KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,              KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,   EE_CLR,
        _______,    UG_NEXT,  KC_BT1,   KC_BT2,   KC_BT3,   KC_2G4,   _______,                      KC_PSCR,  KC_SCRL,  KC_PAUSE, _______,  _______,  UG_HUEU,  UG_HUED,  RL_MOD,
        _______,    _______,  TO(_BL),  KC_NO,    _______,  _______,  _______,                      KC_HOME,  KC_END,   KC_PGUP,  KC_PGDN,  UG_SATD,  UG_SATU,            KC_END,
        _______,    _______,  _______,  UG_TOGG,  _______,  _______,  MM_BATQ,            MM_BATQ,  _______,  _______,  _______,  _______,  _______,  KC_BTPR,  UG_VALU,  KC_INS,
        _______,    KC_FILP,  _______,  KC_LGUI,            KC_BATQ,                                KC_BATQ,            KC_RGUI,  KC_RALT,            UG_SPDU,  UG_VALD,  UG_SPDD),

};


#ifdef ENCODER_MAP_ENABLE
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [0] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},
    [1] = {ENCODER_CCW_CW(_______, _______)},
    [2] = {ENCODER_CCW_CW(_______, _______)},
    [3] = {ENCODER_CCW_CW(_______, _______)},
};
#endif
// clang-format on
