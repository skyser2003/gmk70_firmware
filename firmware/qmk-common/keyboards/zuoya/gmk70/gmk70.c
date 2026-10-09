// Copyright 2024 yangzheng20003 (@yangzheng20003)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "wls/wls.h"
#include "stock_lighting.h"
#include "quantum.h"
#include "process_rgb_matrix.h"
#include "serial_usart.h"
#include "split_diagnostics.h"
#ifdef GMK70_PERSISTENT_DIAGNOSTICS
#    include "sleep_log.h"
#endif
#include "stock_battery.h"
#include "stock_power.h"
#include "stock_v15.h"
void last_matrix_activity_trigger(void);
#ifdef VIA_ENABLE
#    include "via.h"
#    include "dynamic_keymap.h"
#endif
#include "smsg.h"
#include "update/qmk_update.h"
#ifdef WIRELESS_ENABLE
#    include "wireless.h"
#    include "usb_main.h"
#    include "lowpower.h"
#    include "module.h"
#endif

typedef union {
    uint32_t raw;
    struct {
        uint8_t devs;
        uint8_t previous_devs;
        uint8_t last_btdevs;
        uint8_t flag : 1;
        uint8_t sleep_selector : 3;
        uint8_t reserved : 4;
    };
} confinfo_t;
confinfo_t      confinfo;
static uint32_t stock_board_flags;
static void     stock_gui_lock(bool locked) {
    keymap_config.no_gui = locked;
    if (locked)
        stock_board_flags |= 4;
    else
        stock_board_flags &= ~4u;
    eeconfig_update_user(stock_board_flags);
}

static void stock_matrix_power(bool enabled) {
    if (enabled)
        rgb_matrix_enable();
    else
        rgb_matrix_disable();
    gpio_write_pin(LED_POWER_EN_PIN, enabled);
    gpio_write_pin(LED_POWER_EN2_PIN, enabled);
}

typedef struct {
    bool     active;
    uint32_t timer;
    uint32_t interval;
    uint32_t times;
    uint8_t  index;
    RGB      rgb;
    void (*blink_cb)(uint8_t);
} hs_rgb_indicator_t;

enum layers {
    _BL = 0,
    _FL,
    _MBL,
    _MFL,
};

hs_rgb_indicator_t hs_rgb_indicators[HS_RGB_INDICATOR_COUNT];
hs_rgb_indicator_t hs_rgb_bat[HS_RGB_BAT_COUNT];

void user_sync_mms_slave_handler(uint8_t in_buflen, const void *in_data, uint8_t out_buflen, void *out_data);
void rgb_blink_dir(void);
void hs_reset_settings(void);
void rgb_matrix_hs_indicator(void);
void rgb_matrix_hs_indicator_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times);
void rgb_matrix_hs_set_remain_time(uint8_t index, uint8_t remain_time);

#define keymap_is_mac_system() ((get_highest_layer(default_layer_state) == _MBL) || (get_highest_layer(default_layer_state) == _MFL))
#define keymap_is_base_layer() ((get_highest_layer(default_layer_state) == _BL) || (get_highest_layer(default_layer_state) == _FL))

uint32_t        post_init_timer       = 0x00;
bool            inqbat_flag           = false;
bool            mac_status            = false;
bool            charging_state        = false;
bool            bat_full_flag         = false;
bool            enable_bat_indicators = true;
uint32_t        bat_indicator_cnt     = true;
static uint32_t ee_clr_timer          = 0;
static bool     stock_reset_pending;
static bool     stock_bootloader_pending;
bool            test_white_light_flag = false;
bool            lower_sleep           = false;
static bool     stock_usb_sleep_requested;
uint8_t         pov;
uint8_t         buff[] = {14, 8, 2, 1, 1, 1, 1, 1, 1, 1, 0};

bool gmk70_stock_cable_flag(void) {
    return confinfo.flag;
}

void usart_init(void) {
    palSetLineMode(SERIAL_USART_TX_PIN, PAL_MODE_ALTERNATE(SERIAL_USART_TX_PAL_MODE) | PAL_OUTPUT_TYPE_PUSHPULL | PAL_OUTPUT_SPEED_HIGHEST);
    palSetLineMode(SERIAL_USART_RX_PIN, PAL_MODE_ALTERNATE(SERIAL_USART_RX_PAL_MODE) | PAL_OUTPUT_TYPE_PUSHPULL | PAL_OUTPUT_SPEED_HIGHEST);
}

void eeconfig_confinfo_update(uint32_t raw) {
    eeconfig_update_kb(raw);
}

typedef struct _master_to_slave_t {
    uint8_t cmd;
    uint8_t body[4];
} master_to_slave_t;

typedef struct _slave_to_master_t {
    uint8_t resp;
    uint8_t body[4];
} slave_to_master_t;

uint32_t eeconfig_confinfo_read(void) {
    return eeconfig_read_kb();
}

void eeconfig_confinfo_default(void) {
    confinfo.raw           = 0;
    confinfo.flag          = false;
    confinfo.previous_devs = 1;
    confinfo.last_btdevs   = 1;
    stock_board_flags      = 0;
    eeconfig_update_user(0);
    confinfo.sleep_selector = 7;

    // #ifdef WIRELESS_ENABLE
    //     confinfo.devs = DEVS_USB;
    // #endif

    eeconfig_confinfo_update(confinfo.raw);
    confinfo.flag = true;

#ifdef RGBLIGHT_ENABLE
    rgblight_mode(buff[0]);
#endif
}

void master_sync_mms_slave(uint8_t last_mode, uint8_t now_mode, uint8_t reset) {
    master_to_slave_t m2s = {0};
    slave_to_master_t s2m = {0};
    m2s.cmd               = 0x55;
    m2s.body[0]           = last_mode;
    m2s.body[1]           = now_mode;
    m2s.body[2]           = reset;
    if (transaction_rpc_exec(USER_SYNC_MMS, sizeof(m2s), &m2s, sizeof(s2m), &s2m)) {
        if (s2m.resp == 0x00)
            ;
        dprintf("Slave Sleep OK1\n");
    } else {
        dprintf("Slave sync failed1!\n");
    }
}

void eeconfig_confinfo_init(void) {
    confinfo.raw = eeconfig_confinfo_read();
    if (!confinfo.raw || confinfo.raw == UINT32_MAX || confinfo.devs > DEVS_2G4 || confinfo.last_btdevs < DEVS_BT1 || confinfo.last_btdevs > DEVS_BT5) {
        eeconfig_confinfo_default();
    }
    confinfo.flag     = true;
    stock_board_flags = eeconfig_read_user();
    if (stock_board_flags == UINT32_MAX) stock_board_flags = 0;
    keymap_config.no_gui = (stock_board_flags & 4) != 0;
    lpwr_set_timeout_value(stock_power_timeout(confinfo.sleep_selector));
}

uint8_t gmk70_stock_sleep_get(void) {
    return confinfo.sleep_selector;
}

void gmk70_stock_sleep_set(uint8_t selector) {
    confinfo.sleep_selector = selector & 7;
    lpwr_set_timeout_value(stock_power_timeout(confinfo.sleep_selector));
    eeconfig_confinfo_update(confinfo.raw);
}

uint8_t gmk70_stock_nkro_get(void) {
    return keymap_config.nkro;
}

void gmk70_stock_nkro_set(uint8_t enabled) {
    keymap_config.nkro = enabled != 0;
}

uint8_t gmk70_stock_brightness_get(void) {
    return (uint16_t)rgb_matrix_get_val() * 255 / 80;
}

void gmk70_stock_brightness_set(uint8_t value) {
    HSV hsv = rgb_matrix_get_hsv();
    rgb_matrix_sethsv_noeeprom(hsv.h, hsv.s, ((uint16_t)value * 80) >> 8);
}

void gmk70_stock_activity(void) {
    last_matrix_activity_trigger();
    lpwr_activity_trigger();
    if (lpwr_get_state() != LPWR_NORMAL)
        lpwr_set_state(LPWR_WAKEUP);
    else
        lpwr_update_timestamp();
}

void lpwr_state_changed_hook(void) {
    // V15 0x08002890 clears the query flag and requests voltage on assignment.
    inqbat_flag = false;
    md_send_devctrl(MD_SND_CMD_DEVCTRL_INQVOL);
}

void gmk70_stock_request_reset(void) {
    stock_reset_pending = true;
}

void gmk70_stock_request_bootloader(void) {
    stock_bootloader_pending = true;
}

void keyboard_post_init_kb(void) {
    stock_lighting_init();
#ifdef CONSOLE_ENABLE
    debug_enable = true;
    debug_matrix = true;
#endif
    eeconfig_confinfo_init();

#ifdef LED_POWER_EN_PIN
    gpio_set_pin_output(LED_POWER_EN_PIN);
    if (rgb_matrix_is_enabled()) gpio_write_pin_high(LED_POWER_EN_PIN);

#endif

#ifdef LED_POWER_EN2_PIN
    gpio_set_pin_output(LED_POWER_EN2_PIN);
    if (rgb_matrix_is_enabled()) gpio_write_pin_high(LED_POWER_EN2_PIN);
#endif

#ifdef HS_BT_DEF_PIN
    gpio_set_pin_input_high(HS_BT_DEF_PIN);
#endif

#ifdef HS_2G4_DEF_PIN
    gpio_set_pin_input_high(HS_2G4_DEF_PIN);
#endif

#ifdef USB_POWER_EN_PIN
    gpio_write_pin_low(USB_POWER_EN_PIN);
    gpio_set_pin_output(USB_POWER_EN_PIN);
#endif

#ifdef HS_BAT_CABLE_PIN
    gpio_set_pin_input(HS_BAT_CABLE_PIN);
#endif

#ifdef BAT_FULL_PIN
    gpio_set_pin_input_high(BAT_FULL_PIN);
#endif

#ifdef WIRELESS_ENABLE
    wireless_init();
#    if (!(defined(HS_BT_DEF_PIN) && defined(HS_2G4_DEF_PIN)))
    wireless_devs_change(!confinfo.devs, confinfo.devs, false);
#    endif
    post_init_timer = timer_read32();
#endif

    keyboard_post_init_user();

    pov = *md_getp_bat();
    // usart_init();
    transaction_register_rpc(USER_SYNC_MMS, user_sync_mms_slave_handler);
#ifdef GMK70_SPLIT_UPDATE
    gmk_update_init();
#    ifdef GMK70_PERSISTENT_DIAGNOSTICS
    sleep_log_init();
#    endif
#endif
}

#ifdef WIRELESS_ENABLE

void usb_power_connect(void) {
#    ifdef USB_POWER_EN_PIN
    gpio_write_pin_low(USB_POWER_EN_PIN);
#    endif
}

void usb_power_disconnect(void) {
#    ifdef USB_POWER_EN_PIN
    gpio_write_pin_high(USB_POWER_EN_PIN);
#    endif
}

void suspend_power_down_kb(void) {
#    ifdef LED_POWER_EN_PIN
    gpio_write_pin_low(LED_POWER_EN_PIN);
#    endif

#    ifdef LED_POWER_EN2_PIN
    gpio_write_pin_low(LED_POWER_EN2_PIN);
#    endif

    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {
    // ChibiOS dispatches physical USB resume through this callback, after its
    // IRQ has returned. Feed the accepted USB cause into the stock class gate.
    if (USB_DRIVER.state == USB_ACTIVE && (lpwr_get_state() == LPWR_PRESLEEP || lpwr_get_state() == LPWR_STOP)) {
        lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_USB);
        if (lpwr_get_prepare_class() == LPWR_PREPARE_ORDINARY)
            lpwr_set_state(LPWR_WAKEUP);
        else
            lpwr_stop_hook_post();
    }
#    ifdef LED_POWER_EN_PIN
    if (rgb_matrix_is_enabled()) gpio_write_pin_high(LED_POWER_EN_PIN);
#    endif

#    ifdef LED_POWER_EN2_PIN
    if (rgb_matrix_is_enabled()) gpio_write_pin_high(LED_POWER_EN2_PIN);
#    endif

    wireless_devs_change(wireless_get_current_devs(), wireless_get_current_devs(), false);
    suspend_wakeup_init_user();
    hs_rgb_blink_set_timer(timer_read32());
}

void suspend_wakeup_init_user(void) {
    usart_init();
    // Stock wakes the peer by restoring the split line, not by sending CC.
}

void suspend_power_down_user(void) {
    if (lpwr_get_state() != LPWR_NORMAL) return;
    if (!is_keyboard_master()) {
        return;
    }
    master_to_slave_t m2s = {0};
    slave_to_master_t s2m = {0};
    m2s.cmd               = 0xBB;
    if (transaction_rpc_exec(USER_SYNC_MMS, sizeof(m2s), &m2s, sizeof(s2m), &s2m)) {
        if (s2m.resp == 0x00) {
        }
        dprintf("Slave Sleep OK\n");
    } else {
        dprint("Slave sync failed!\n");
    }
}

bool lpwr_is_allow_timeout_hook(void) {
#    ifdef GMK70_SPLIT_UPDATE
    if (gmk_update_active()) return false;
#    endif
    return true;
}

bool lpwr_timeout_accept_hook(void) {
    // V15 0x08002BD4: capture cable on master, force it on for the peer.
    bool master   = is_keyboard_master();
    confinfo.flag = master ? gpio_read_pin(HS_BAT_CABLE_PIN) : true;
    return confinfo.devs != DEVS_USB || USB_DRIVER.state != USB_ACTIVE;
}

uint8_t lpwr_requested_classes_hook(void) {
#    ifdef GMK70_SPLIT_UPDATE
    if (gmk_update_active()) return LPWR_PREPARE_ORDINARY;
#    endif
    // Bit mask of the two independent special requests; USB is applied last.
    return (stock_usb_sleep_requested ? LPWR_PREPARE_USB : 0) | (lower_sleep ? LPWR_PREPARE_CRITICAL : 0);
}

void gmk70_stock_usb_sleep_set(bool requested) {
    stock_usb_sleep_requested = requested;
    if (requested) split_diagnostics_event(SPLIT_DIAG_SLEEP_REASON, SPLIT_DIAG_USB_SPECIAL, 0, 0);
    // V15 0x08003CBC: cancellation wakes NORMAL/PRE/STOP and clears the class.
    if (!requested && lpwr_get_state() <= LPWR_STOP) {
        lpwr_set_state(LPWR_WAKEUP);
        lpwr_set_prepare_class(LPWR_PREPARE_ORDINARY);
    }
}

bool lpwr_is_allow_presleep_hook(void) {
    return stock_power_prepare_allowed(lpwr_get_prepare_class() != LPWR_PREPARE_ORDINARY);
}

bool lpwr_is_allow_stop_hook(void) {
    lpwr_prepare_t preparation = lpwr_get_prepare_class();
    bool           bypass      = preparation == LPWR_PREPARE_CRITICAL || (preparation == LPWR_PREPARE_USB && lpwr_get_previous_state() == LPWR_PRESLEEP);
    return stock_power_prepare_allowed(bypass);
}

void lpwr_usb_prepare_hook(void) {
    // V15 0x08003D58 resets the matrix and its held-key counter once.
    // Its old internal key-record buffers have no public reset in modern QMK.
    matrix_init();
    stock_power_reset_keys();
}

void lpwr_power_down_hook(void) {
    // Stock prepares inline. Avoid ChibiOS suspend_power_down's extra 17 ms.
    suspend_power_down_kb();
}

void lpwr_clock_enable_user(void) {
    // V15 restores A9/A10 before re-enabling interrupts, even on rejected wakes.
    usart_init();
}

bool lpwr_host_resume_allowed_hook(void) {
    return lpwr_get_prepare_class() == LPWR_PREPARE_ORDINARY;
}

static uint8_t stock_presleep_device;

bool lpwr_can_enter_stop_hook(void) {
    return wireless_get_current_devs() == stock_presleep_device && !(stock_presleep_device == DEVS_USB && confinfo.flag);
}

void lpwr_task_hook_pre(void) {
    // V15 refreshes this runtime bit on every master power-task pass.
    if (is_keyboard_master()) confinfo.flag = gpio_read_pin(HS_BAT_CABLE_PIN);
}

void lpwr_presleep_hook(void) {
    if (is_keyboard_master()) {
        master_to_slave_t m2s = {0};
        slave_to_master_t s2m = {0};
        m2s.cmd               = 0xAA;
        // V15 sends an ordinary peer request in every local preparation class.
        // A peer must retain its own battery protection, not inherit ours.
        if (transaction_rpc_exec(USER_SYNC_MMS, sizeof(m2s), &m2s, sizeof(s2m), &s2m)) {
            if (s2m.resp == 0x00) {
            }
            dprintf("Slave Sleep OK\n");
        } else {
            dprint("Slave sync failed!\n");
        }
        if (wireless_get_current_devs() != DEVS_USB) {
            // Only a wireless master changes the split pins after sending AA.
            palSetLineMode(SERIAL_USART_RX_PIN, PAL_OUTPUT_TYPE_OPENDRAIN);
            palSetLineMode(SERIAL_USART_TX_PIN, PAL_OUTPUT_TYPE_OPENDRAIN);
        }
    }
    stock_presleep_device = wireless_get_current_devs();

    if ((wireless_get_current_devs() == DEVS_USB) && (!confinfo.flag)) {
        if (USB_DRIVER.state != USB_STOP) {
            usb_power_disconnect();
            usbDisconnectBus(&USBD1);
            usbStop(&USBD1);
        }
    }
}

void wireless_post_task(void) {
    // The selector is on the master; the other half receives mode over split RPC.
    if (!is_keyboard_master()) {
        return;
    }
    // auto switching devs
    if (post_init_timer && timer_elapsed32(post_init_timer) >= 2000) {
        md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_BT_EN);  // timeout 30min to sleep in bt mode, enable
        md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_2G4_EN); // timeout 30min to sleep in 2.4g mode, enable
        md_send_devctrl(MD_SND_CMD_DEVCTRL_FW_VERSION);   // get the module fw version.
        wireless_devs_change(!confinfo.devs, confinfo.devs, false);
        post_init_timer = 0x00;
    }
#    if defined(HS_BT_DEF_PIN) && defined(HS_2G4_DEF_PIN)
    hs_mode_scan(false, confinfo.devs, confinfo.last_btdevs);
#    endif
}

static uint16_t       pairing_keycode;
static deferred_token pairing_token = INVALID_DEFERRED_TOKEN;

static bool selected_device_allowed(uint8_t device) {
    uint8_t mode = confinfo.devs;
    hs_modeio_detection(true, &mode, confinfo.last_btdevs);
    return gmk70_device_allowed(device, mode);
}

static uint32_t wls_process_long_press(uint32_t trigger_time, void *cb_arg) {
    (void)trigger_time;
    const uint16_t keycode = *(const uint16_t *)cb_arg;
    const uint8_t  device  = gmk70_wireless_device(keycode);
    pairing_token          = INVALID_DEFERRED_TOKEN;
    if (device != DEVS_USB && selected_device_allowed(device)) {
        wireless_devs_change(wireless_get_current_devs(), device, true);
    }
    return 0;
}

bool process_record_wls(uint16_t keycode, keyrecord_t *record) {
    const uint8_t device = gmk70_wireless_device(keycode);
    if (device == UINT8_MAX) {
        return true;
    }
    if (!record->event.pressed) {
        if (keycode == pairing_keycode && pairing_token != INVALID_DEFERRED_TOKEN) {
            cancel_deferred_exec(pairing_token);
            pairing_token = INVALID_DEFERRED_TOKEN;
        }
        return false;
    }
    if (!selected_device_allowed(device)) {
        return false;
    }
    if (pairing_token != INVALID_DEFERRED_TOKEN) {
        cancel_deferred_exec(pairing_token);
        pairing_token = INVALID_DEFERRED_TOKEN;
    }
    if (wireless_get_current_devs() != device) {
        wireless_devs_change(wireless_get_current_devs(), device, false);
    }
    if (device != DEVS_USB) {
        pairing_keycode = keycode;
        pairing_token   = defer_exec(3000, wls_process_long_press, &pairing_keycode);
    }
    hs_rgb_blink_set_timer(timer_read32());
    return false;
}
#endif

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (test_white_light_flag && record->event.pressed) {
        test_white_light_flag = false;
        rgb_matrix_set_color_all(0x00, 0x00, 0x00);
    }

    if (*md_getp_state() == MD_STATE_CONNECTED) {
        hs_rgb_blink_set_timer(timer_read32());
    }

    return true;
}

uint32_t    hs_ct_time;
RGB         rgb_test_open;
static bool process_gmk70_record(uint16_t keycode, keyrecord_t *record) {
    if (process_record_user(keycode, record) != true) {
        return false;
    }

#ifdef WIRELESS_ENABLE
    if (process_record_wls(keycode, record) != true) {
        return false;
    }
#endif
    const uint16_t shortcut = gmk70_shortcut_keycode(keycode);
    if (shortcut != KC_NO) {
        if (record->event.pressed) {
            register_code16(shortcut);
        } else {
            unregister_code16(shortcut);
        }
        return false;
    }
    switch (keycode) {
        case GU_TOGG:
        case RM_TOGG:
            if (record->event.pressed) {
                uint8_t command;
                if (keycode == GU_TOGG) {
                    stock_gui_lock(!keymap_config.no_gui);
                    command = keymap_config.no_gui ? 0xAE : 0xAF;
                } else {
                    stock_matrix_power(!rgb_matrix_is_enabled());
                    command = rgb_matrix_is_enabled() ? 0xC1 : 0xC2;
                }
                master_to_slave_t request  = {.cmd = command};
                slave_to_master_t response = {0};
                (void)transaction_rpc_exec(USER_SYNC_MMS, sizeof(request), &request, sizeof(response), &response);
            }
            return false;
        case KC_F1: {
            if ((stock_board_flags & 0x10)) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_MSEL);
                    } else {
                        unregister_code16(KC_MSEL);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F2: {
            if ((stock_board_flags & 0x10)) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_VOLD);
                    } else {
                        unregister_code16(KC_VOLD);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F3: {
            if ((stock_board_flags & 0x10)) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_VOLU);
                    } else {
                        unregister_code16(KC_VOLU);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F4: {
            if ((stock_board_flags & 0x10)) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_MUTE);
                    } else {
                        unregister_code16(KC_MUTE);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F5: {
            if ((stock_board_flags & 0x10)) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_MSTP);
                    } else {
                        unregister_code16(KC_MSTP);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F6: {
            if ((stock_board_flags & 0x10)) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_MPRV);
                    } else {
                        unregister_code16(KC_MPRV);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F7: {
            if ((stock_board_flags & 0x10)) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_MPLY);
                    } else {
                        unregister_code16(KC_MPLY);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F8: {
            if ((stock_board_flags & 0x10)) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_MNXT);
                    } else {
                        unregister_code16(KC_MNXT);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F9: {
            if ((stock_board_flags & 0x10)) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_MAIL);
                    } else {
                        unregister_code16(KC_MAIL);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F10: {
            if ((stock_board_flags & 0x10)) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_WHOM);
                    } else {
                        unregister_code16(KC_WHOM);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F11: {
            if ((stock_board_flags & 0x10)) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_CALC);
                    } else {
                        unregister_code16(KC_CALC);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_F12: {
            if ((stock_board_flags & 0x10)) {
                if (keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code16(KC_WSCH);
                    } else {
                        unregister_code16(KC_WSCH);
                    }
                    return false;
                }
            }
            return true;
        } break;
        case KC_1: {
            if ((stock_board_flags & 0x10)) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F1);
                    } else {
                        unregister_code(KC_F1);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_BRID);
                    } else {
                        unregister_code(KC_BRID);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_2: {
            if ((stock_board_flags & 0x10)) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F2);
                    } else {
                        unregister_code(KC_F2);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_BRIU);
                    } else {
                        unregister_code(KC_BRIU);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_3: {
            if ((stock_board_flags & 0x10)) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F3);
                    } else {
                        unregister_code(KC_F3);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_LGUI);
                        register_code(KC_TAB);
                    } else {
                        unregister_code(KC_LGUI);
                        unregister_code(KC_TAB);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_4: {
            if ((stock_board_flags & 0x10)) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F4);
                    } else {
                        unregister_code(KC_F4);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_LGUI);
                        register_code(KC_E);
                    } else {
                        unregister_code(KC_LGUI);
                        unregister_code(KC_E);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_5: {
            if ((stock_board_flags & 0x10)) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F5);
                    } else {
                        unregister_code(KC_F5);
                    }
                } else {
                    if (record->event.pressed) {
                        rgb_matrix_decrease_val();
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_6: {
            if ((stock_board_flags & 0x10)) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F6);
                    } else {
                        unregister_code(KC_F6);
                    }
                } else {
                    if (record->event.pressed) {
                        rgb_matrix_increase_val();
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_7: {
            if ((stock_board_flags & 0x10)) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F7);
                    } else {
                        unregister_code(KC_F7);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_MPRV);
                    } else {
                        unregister_code(KC_MPRV);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_8: {
            if ((stock_board_flags & 0x10)) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F8);
                    } else {
                        unregister_code(KC_F8);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_MPLY);
                    } else {
                        unregister_code(KC_MPLY);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_9: {
            if ((stock_board_flags & 0x10)) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F9);
                    } else {
                        unregister_code(KC_F9);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_MNXT);
                    } else {
                        unregister_code(KC_MNXT);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_0: {
            if ((stock_board_flags & 0x10)) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F10);
                    } else {
                        unregister_code(KC_F10);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_MUTE);
                    } else {
                        unregister_code(KC_MUTE);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_MINS: {
            if ((stock_board_flags & 0x10)) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F11);
                    } else {
                        unregister_code(KC_F11);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_VOLD);
                    } else {
                        unregister_code(KC_VOLD);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_EQL: {
            if ((stock_board_flags & 0x10)) {
                if (!keymap_is_mac_system()) {
                    if (record->event.pressed) {
                        register_code(KC_F12);
                    } else {
                        unregister_code(KC_F12);
                    }
                } else {
                    if (record->event.pressed) {
                        register_code(KC_VOLU);
                    } else {
                        unregister_code(KC_VOLU);
                    }
                }
                return false;
            }
            return true;
        } break;
        case KC_FILP: {
            if (record->event.pressed) {
                stock_board_flags ^= 0x10;
                eeconfig_update_user(stock_board_flags);
            }
            return false;
        } break;
        case KC_BATQ:
        case HS_BATQ:
            stock_battery_key(keycode, record->event.pressed, confinfo.devs != DEVS_USB);
            return false;

        case STOCK_USB_SLEEP_OFF:
        case STOCK_USB_SLEEP_ON:
        case STOCK_USB_SLEEP_TOGGLE:
            if (record->event.pressed) {
                gmk70_stock_usb_sleep_set(keycode == STOCK_USB_SLEEP_TOGGLE ? !stock_usb_sleep_requested : keycode == STOCK_USB_SLEEP_ON);
            }
            return false;

        case NK_TOGG: {
            if (record->event.pressed) {
                rgb_matrix_hs_indicator_set(0xFF, (RGB){0x00, 0x6E, 0x00}, 250, 1);
            }
        } break;
        case RL_MOD: {
            if (record->event.pressed) {
                stock_lighting_step();
                master_to_slave_t request  = {.cmd = 0xA6};
                slave_to_master_t response = {0};
                (void)transaction_rpc_exec(USER_SYNC_MMS, sizeof(request), &request, sizeof(response), &response);
            }

            return false;
        } break;
        case EE_CLR: {
            if (record->event.pressed) {
                ee_clr_timer = timer_read32();
            } else {
                ee_clr_timer = 0;
            }

            return false;
        } break;
        case RM_SPDU: {
            if (record->event.pressed) {
                if (rgb_matrix_get_speed() == 255) {
                    rgb_blink_dir();
                }
            }
        } break;
        case RM_SPDD: {
            if (record->event.pressed) {
                if (rgb_matrix_get_speed() <= 51) {
                    rgb_blink_dir();
                    rgb_matrix_set_speed(51);
                    return false;
                }
            }
        } break;
        case RM_VALU: {
            if (record->event.pressed) {
                if (rgb_matrix_get_val() >= 64) {
                    rgb_blink_dir();
                }
            }
        } break;
        case RM_VALD: {
            if (record->event.pressed) {
                if (rgb_matrix_get_val() <= 16) {
                    rgb_blink_dir();
                }
            }
        } break;

        case TO(_BL): {
            if (record->event.pressed) {
                rgb_matrix_hs_set_remain_time(HS_RGB_BLINK_INDEX_MAC, 0);
                rgb_matrix_hs_indicator_set(HS_RGB_BLINK_INDEX_WIN, (RGB){RGB_WHITE}, 250, 3);
                if (keymap_is_mac_system()) {
                    set_single_persistent_default_layer(_BL);
                    layer_move(0);
                }
            }

            return false;
        } break;
        case TO(_MBL): {
            if (record->event.pressed) {
                rgb_matrix_hs_set_remain_time(HS_RGB_BLINK_INDEX_WIN, 0);
                rgb_matrix_hs_indicator_set(HS_RGB_BLINK_INDEX_MAC, (RGB){RGB_WHITE}, 250, 3);
                if (!keymap_is_mac_system()) {
                    set_single_persistent_default_layer(_MBL);
                    layer_move(0);
                }
            }

            return false;
        } break;

        case RM_NEXT: {
            if (record->event.pressed) {
                uint8_t mode = rgb_matrix_get_mode();
                if (mode == 29) {
                    rgb_matrix_mode(31);
                    return false;
                }
            }
            return true;

            return false;
        } break;
        case KC_LCMD: {
            if (keymap_is_mac_system()) {
                if (keymap_config.no_gui) {
                    if (record->event.pressed) {
                        register_code16(KC_LCMD);
                    } else {
                        unregister_code16(KC_LCMD);
                    }
                }
            }

            return true;
        } break;
        case KC_RCMD: {
            if (keymap_is_mac_system()) {
                if (keymap_config.no_gui) {
                    if (record->event.pressed) {
                        register_code16(KC_RCMD);
                    } else {
                        unregister_code16(KC_RCMD);
                    }
                }
            }

            return true;
        } break;
        default:
            break;
    }

    return true;
}

// Saved maps from pre-0.28 QMK used underglow codes for matrix lighting.
// Pass their translated value through both the board hooks and upstream handler.
bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
#ifndef VIAL_ENABLE
    // Modern QMK has no process_action_kb callback. Invoke wired wake here
    // before filtering a suspended-host record.
    if (confinfo.devs == DEVS_USB) usb_remote_host();
    if (record->event.pressed) lpwr_activity_trigger();
    stock_power_key(record->event.pressed);
#endif
    // V15's wired key hook suppresses basic keys while the host is suspended.
    // The wired hook already requested remote wake before this gate.
    if (confinfo.devs == DEVS_USB && USB_DRIVER.state == USB_SUSPENDED && keycode <= 0xFF) return false;
    // V15 board gate 0x08005420 runs before ordinary key processing. Special
    // controls remain available for reconnecting; report delivery is not gated.
    if (confinfo.devs != DEVS_USB && *md_getp_state() != MD_STATE_CONNECTED && keycode >= 4 && keycode <= 0xA4) return false;
    uint16_t translated = gmk70_rgb_keycode(keycode);
    if (!process_gmk70_record(translated, record)) {
        return false;
    }
    return translated == keycode || process_rgb_matrix(translated, record);
}

void housekeeping_task_user(void) { // loop
    if (is_keyboard_master() && lpwr_get_state() == LPWR_NORMAL) {
        if (stock_bootloader_pending) {
            stock_bootloader_pending = false;
            reset_keyboard();
        }
        if (stock_reset_pending && !smsg_is_busy()) {
            stock_reset_pending = false;
            hs_reset_settings();
        }
    }
#ifdef GMK70_SPLIT_UPDATE
    gmk_update_task();
#endif
    split_diagnostics_loop();
    uint8_t         hs_now_mode;
    static uint32_t hs_current_time;

    charging_state = gpio_read_pin(HS_BAT_CABLE_PIN);

    bat_full_flag = gpio_read_pin(BAT_FULL_PIN);

    if (confinfo.flag && (bat_full_flag)) {
        hs_now_mode = MD_SND_CMD_DEVCTRL_CHARGING_DONE;
    } else if (confinfo.flag) {
        hs_now_mode = MD_SND_CMD_DEVCTRL_CHARGING;
    } else {
        hs_now_mode = MD_SND_CMD_DEVCTRL_CHARGING_STOP;
    }

    if (!hs_current_time || timer_elapsed32(hs_current_time) > 1000) {
        hs_current_time = timer_read32();
        md_send_devctrl(hs_now_mode);
    }

    if (is_keyboard_master()) {
        static uint32_t last_sync = 0;
        if (timer_elapsed32(last_sync) > 2000) {
            last_sync = timer_read32();
            pov       = *md_getp_bat();
            master_sync_mms_slave(wireless_get_current_devs(), wireless_get_current_devs(), pov);
        }
    }
}

#ifdef RGB_MATRIX_ENABLE

#    ifdef WIRELESS_ENABLE
bool     wls_rgb_indicator_reset    = false;
uint32_t wls_rgb_indicator_timer    = 0x00;
uint32_t wls_rgb_indicator_interval = 0;
uint32_t wls_rgb_indicator_times    = 0;
uint32_t wls_rgb_indicator_index    = 0;
RGB      wls_rgb_indicator_rgb      = {0};

void rgb_matrix_wls_indicator_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {
    wls_rgb_indicator_timer = timer_read32();

    wls_rgb_indicator_index    = index;
    wls_rgb_indicator_interval = interval;
    wls_rgb_indicator_times    = times * 2;
    wls_rgb_indicator_rgb      = rgb;
}

void wireless_devs_change_kb(uint8_t old_devs, uint8_t new_devs, bool reset) {
    wls_rgb_indicator_reset = reset;

    if (confinfo.devs != wireless_get_current_devs()) {
        confinfo.devs = wireless_get_current_devs();
        if (confinfo.devs) confinfo.previous_devs = confinfo.devs;
        if (confinfo.devs >= DEVS_BT1 && confinfo.devs <= DEVS_BT5) confinfo.last_btdevs = confinfo.devs;
        eeconfig_confinfo_update(confinfo.raw);
    }

    switch (new_devs) {
        case DEVS_BT1: {
            if (reset) {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT1, (RGB){HS_LBACK_COLOR_BT1}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT1, (RGB){HS_PAIR_COLOR_BT1}, 500, 1);
            }
        } break;
        case DEVS_BT2: {
            if (reset) {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT2, (RGB){HS_LBACK_COLOR_BT2}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT2, (RGB){HS_PAIR_COLOR_BT2}, 500, 1);
            }
        } break;
        case DEVS_BT3: {
            if (reset) {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT3, (RGB){HS_LBACK_COLOR_BT3}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT3, (RGB){HS_PAIR_COLOR_BT3}, 500, 1);
            }
        } break;
        case DEVS_BT4: {
            if (reset) {
                rgb_matrix_wls_indicator_set(41, (RGB){RGB_BLUE}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(41, (RGB){RGB_BLUE}, 500, 1);
            }
        } break;
        case DEVS_BT5: {
            if (reset) {
                rgb_matrix_wls_indicator_set(42, (RGB){RGB_BLUE}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(42, (RGB){RGB_BLUE}, 500, 1);
            }
        } break;
        case DEVS_2G4: {
            if (reset) {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_2G4, (RGB){HS_LBACK_COLOR_2G4}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_2G4, (RGB){HS_LBACK_COLOR_2G4}, 500, 1);
            }
        } break;
        default:
            break;
    }
}

bool rgb_matrix_wls_indicator_cb(void) {
    if (*md_getp_state() != MD_STATE_CONNECTED) {
        wireless_devs_change_kb(wireless_get_current_devs(), wireless_get_current_devs(), wls_rgb_indicator_reset);
        return true;
    }

    // refresh led
    led_wakeup();

    return false;
}

void rgb_matrix_wls_indicator(void) {
    if (wls_rgb_indicator_timer) {
        if (timer_elapsed32(wls_rgb_indicator_timer) >= wls_rgb_indicator_interval) {
            wls_rgb_indicator_timer = timer_read32();

            if (wls_rgb_indicator_times) {
                wls_rgb_indicator_times--;
            }

            if (wls_rgb_indicator_times <= 0) {
                wls_rgb_indicator_timer = 0x00;
                if (rgb_matrix_wls_indicator_cb() != true) {
                    return;
                }
            }
        }

        if (wls_rgb_indicator_times % 2) {
            rgb_matrix_set_color(wls_rgb_indicator_index, wls_rgb_indicator_rgb.r, wls_rgb_indicator_rgb.g, wls_rgb_indicator_rgb.b);
        } else {
            rgb_matrix_set_color(wls_rgb_indicator_index, 0x00, 0x00, 0x00);
        }
    }
}

void rgb_matrix_hs_bat_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {
    for (int i = 0; i < HS_RGB_BAT_COUNT; i++) {
        if (!hs_rgb_bat[i].active) {
            hs_rgb_bat[i].active   = true;
            hs_rgb_bat[i].timer    = timer_read32();
            hs_rgb_bat[i].interval = interval;
            hs_rgb_bat[i].times    = times * 2;
            hs_rgb_bat[i].index    = index;
            hs_rgb_bat[i].rgb      = rgb;
            break;
        }
    }
}

void rgb_matrix_hs_bat(void) {
    for (int i = 0; i < HS_RGB_BAT_COUNT; i++) {
        if (hs_rgb_bat[i].active) {
            if (timer_elapsed32(hs_rgb_bat[i].timer) >= hs_rgb_bat[i].interval) {
                hs_rgb_bat[i].timer = timer_read32();

                if (hs_rgb_bat[i].times) {
                    hs_rgb_bat[i].times--;
                }

                if (hs_rgb_bat[i].times <= 0) {
                    hs_rgb_bat[i].active = false;
                    hs_rgb_bat[i].timer  = 0x00;
                }
            }

            if (hs_rgb_bat[i].times % 2) {
                rgb_matrix_set_color(hs_rgb_bat[i].index, hs_rgb_bat[i].rgb.r, hs_rgb_bat[i].rgb.g, hs_rgb_bat[i].rgb.b);
            } else {
                rgb_matrix_set_color(hs_rgb_bat[i].index, 0x00, 0x00, 0x00);
            }
        }
    }
}
bool temp, im_test_rate_flag;
void bat_indicators(void) {
    static uint32_t battery_process_time = 0;

    if (!is_keyboard_master()) {
        return;
    }

    if (confinfo.flag && (bat_full_flag)) {
        battery_process_time = 0;
        if (stock_battery_charging_requested()) rgb_matrix_set_color(HS_MATRIX_BLINK_INDEX_BAT, 0x00, 80, 0x00);
    } else if (confinfo.flag) {
        battery_process_time = 0;
        if (stock_battery_charging_requested()) rgb_matrix_set_color(HS_MATRIX_BLINK_INDEX_BAT, 80, 0x00, 0x00);
    } else if (pov <= BATTERY_CAPACITY_LOW) {
        rgb_matrix_hs_bat_set(HS_MATRIX_BLINK_INDEX_BAT, (RGB){0xFF, 0x00, 0x00}, 250, 1);

        if (pov <= BATTERY_CAPACITY_STOP) {
            if (!battery_process_time) {
                battery_process_time = timer_read32();
            }

            if (battery_process_time && timer_elapsed32(battery_process_time) > 60000) {
                battery_process_time = 0;
                lower_sleep          = true;
                split_diagnostics_event(SPLIT_DIAG_SLEEP_REASON, SPLIT_DIAG_LOW_BATTERY, pov, 0);
            }
        }
    } else {
        battery_process_time = 0;
    }
}

#    endif

#endif

void rgb_blink_dir(void) {
    rgb_matrix_hs_indicator_set(54, (RGB){0, 0, 0}, 250, 1);
    rgb_matrix_hs_indicator_set(66, (RGB){0, 0, 0}, 250, 1);
    rgb_matrix_hs_indicator_set(67, (RGB){0, 0, 0}, 250, 1);
    rgb_matrix_hs_indicator_set(65, (RGB){0, 0, 0}, 250, 1);
}

bool hs_reset_settings_user(void) {
    rgb_matrix_hs_indicator_set(0xFF, (RGB){0x10, 0x10, 0x10}, 250, 3);

    return true;
}

void nkr_indicators_hook(uint8_t index) {
    if ((hs_rgb_indicators[index].rgb.r == 0x6E) && (hs_rgb_indicators[index].rgb.g == 0x00) && (hs_rgb_indicators[index].rgb.b == 0x00)) {
        rgb_matrix_hs_indicator_set(0xFF, (RGB){0x6E, 0x00, 0x00}, 250, 1);

    } else if ((hs_rgb_indicators[index].rgb.r == 0x00) && (hs_rgb_indicators[index].rgb.g == 0x6E) && (hs_rgb_indicators[index].rgb.b == 0x00)) {
        rgb_matrix_hs_indicator_set(0xFF, (RGB){0x00, 0x00, 0x6F}, 250, 1);
    }
}

void rgb_matrix_hs_indicator_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {
    for (int i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
        if (!hs_rgb_indicators[i].active) {
            hs_rgb_indicators[i].active   = true;
            hs_rgb_indicators[i].timer    = timer_read32();
            hs_rgb_indicators[i].interval = interval;
            hs_rgb_indicators[i].times    = times * 2;
            hs_rgb_indicators[i].index    = index;
            hs_rgb_indicators[i].rgb      = rgb;
            if (index != 0xFF)
                hs_rgb_indicators[i].blink_cb = NULL;
            else {
                hs_rgb_indicators[i].blink_cb = nkr_indicators_hook;
            }
            break;
        }
    }
}

void rgb_matrix_hs_set_remain_time(uint8_t index, uint8_t remain_time) {
    for (int i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
        if (hs_rgb_indicators[i].index == index) {
            hs_rgb_indicators[i].times  = 0;
            hs_rgb_indicators[i].active = false;
            break;
        }
    }
}

void rgb_matrix_hs_indicator(void) {
    for (int i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
        if (hs_rgb_indicators[i].active) {
            if (timer_elapsed32(hs_rgb_indicators[i].timer) >= hs_rgb_indicators[i].interval) {
                hs_rgb_indicators[i].timer = timer_read32();

                if (hs_rgb_indicators[i].times) {
                    hs_rgb_indicators[i].times--;
                }

                if (hs_rgb_indicators[i].times <= 0) {
                    hs_rgb_indicators[i].active = false;
                    hs_rgb_indicators[i].timer  = 0x00;
                    if (hs_rgb_indicators[i].blink_cb != NULL) hs_rgb_indicators[i].blink_cb(i);
                    continue;
                }
            }

            if ((hs_rgb_indicators[i].times % 2)) {
                if (hs_rgb_indicators[i].index == 0xFF) {
                    rgb_matrix_set_color_all(hs_rgb_indicators[i].rgb.r, hs_rgb_indicators[i].rgb.g, hs_rgb_indicators[i].rgb.b);
                } else {
                    rgb_matrix_set_color(hs_rgb_indicators[i].index, hs_rgb_indicators[i].rgb.r, hs_rgb_indicators[i].rgb.g, hs_rgb_indicators[i].rgb.b);
                }
            } else {
                if (hs_rgb_indicators[i].index == 0xFF) {
                    rgb_matrix_set_color_all(0x00, 0x00, 0x00);
                } else {
                    rgb_matrix_set_color(hs_rgb_indicators[i].index, 0x00, 0x00, 0x00);
                }
            }
        }
    }
}

bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
    if (test_white_light_flag) {
        RGB rgb_test_open = hsv_to_rgb((HSV){.h = 0, .s = 0, .v = RGB_MATRIX_VAL_STEP * 5});
        rgb_matrix_set_color_all(rgb_test_open.r, rgb_test_open.g, rgb_test_open.b);

        return false;
    }
#ifdef RGBLIGHT_ENABLE
    if (rgb_matrix_indicators_advanced_user(led_min, led_max) != true) {
        return false;
    }
#endif

    if (ee_clr_timer && timer_elapsed32(ee_clr_timer) > 3000) {
        hs_reset_settings();
        ee_clr_timer = 0;
    }

    stock_lighting_render();
    if ((stock_board_flags & 0x10)) rgb_matrix_set_color(32, 80, 80, 80);
    stock_battery_render(pov);
    if (host_keyboard_led_state().caps_lock) rgb_matrix_set_color(HS_RGB_INDEX_CAPS, 80, 80, 80);

    if (!keymap_is_mac_system() && keymap_config.no_gui) rgb_matrix_set_color(HS_RGB_INDEX_WIN_LOCK, 80, 80, 80);

#ifdef WIRELESS_ENABLE
    rgb_matrix_wls_indicator();

    if (enable_bat_indicators && !inqbat_flag) {
        rgb_matrix_hs_bat();
        bat_indicators();
        bat_indicator_cnt = timer_read32();
    }

    if (!enable_bat_indicators) {
        if (timer_elapsed32(bat_indicator_cnt) > 2000) {
            enable_bat_indicators = true;
            bat_indicator_cnt     = timer_read32();
        }
    }

#endif

    rgb_matrix_hs_indicator();
    return true;
}

void hs_reset_settings(void) {
    if (is_keyboard_master()) {
        master_to_slave_t m2s = {0};
        slave_to_master_t s2m = {0};
        m2s.cmd               = 0xDD;
        if (transaction_rpc_exec(USER_SYNC_MMS, sizeof(m2s), &m2s, sizeof(s2m), &s2m)) {
            if (s2m.resp == 0x00) {
            }
            dprintf("Slave Sleep OK\n");
        } else {
            dprintf("Slave sync failed!\n");
        }
    }
    enable_bat_indicators = false;
    eeconfig_init();
    eeconfig_update_rgb_matrix_default();

#ifdef RGBLIGHT_ENABLE
    extern void rgblight_init(void);
    is_rgblight_initialized = false;
    rgblight_init();
    eeconfig_update_rgblight_default();
    rgblight_enable();
#endif

    eeconfig_read_keymap(&keymap_config);

#if defined(NKRO_ENABLE) && defined(FORCE_NKRO)
    keymap_config.nkro = 0;
    eeconfig_update_keymap(&keymap_config);
#endif

    // #if defined(WIRELESS_ENABLE)
    //     wireless_devs_change(wireless_get_current_devs(), DEVS_USB, false);
    // #endif

    if (hs_reset_settings_user() != true) {
        return;
    }
    hs_rgb_blink_set_timer(timer_read32());
    // Stock settings reset does not create another radio thread or re-register
    // split transactions. Restore state in the existing keyboard instance.
    eeconfig_confinfo_init();
#ifdef VIA_ENABLE
    dynamic_keymap_reset();
    dynamic_keymap_macro_reset();
    via_eeprom_set_valid(true);
#endif
}

void lpwr_wakeup_hook(void) {
    if (is_keyboard_master()) {
        hs_mode_scan(false, confinfo.devs, confinfo.last_btdevs);
    }

    if (rgb_matrix_is_enabled()) {
        gpio_write_pin_high(LED_POWER_EN_PIN);
        gpio_write_pin_high(LED_POWER_EN2_PIN);
    } else {
        gpio_write_pin_low(LED_POWER_EN_PIN);
        gpio_write_pin_low(LED_POWER_EN2_PIN);
    }
}

void user_sync_mms_slave_handler(uint8_t in_buflen, const void *in_data, uint8_t out_buflen, void *out_data) {
    if (in_buflen != sizeof(master_to_slave_t) || out_buflen != sizeof(slave_to_master_t)) {
        return;
    }
    const master_to_slave_t *m2s = (const master_to_slave_t *)in_data;
    slave_to_master_t       *s2m = (slave_to_master_t *)out_data;
    split_diagnostics_received(m2s->cmd);
    if (stock_battery_receive(m2s->cmd, &s2m->resp)) return;

    switch (m2s->cmd) {
        case 0x55: // sync multimode
            wireless_devs_change(m2s->body[0], m2s->body[1], false);
            pov       = m2s->body[2];
            s2m->resp = 0x00;
            split_diagnostics_reply(s2m->body);
            break;
        case 0xAA:
            s2m->resp = 0x00;
            // V15 ignores all AA body bytes; its local critical flag is separate.
            lpwr_set_timeout_manual(true);
            break;
        case 0xBB:
            gpio_write_pin_low(A5);
            gpio_write_pin_low(A8);
            s2m->resp = 0x00;
            break;
        case 0xCC:
            gpio_write_pin_high(A5);
            gpio_write_pin_high(A8);
            s2m->resp = 0x00;
            break;
        case 0xDD:
            hs_reset_settings();
            s2m->resp = 0x00;
            break;
        case 0xA6:
            stock_lighting_step();
            s2m->resp = 0;
            break;
        case 0xAE:
        case 0xAF:
            stock_gui_lock(m2s->cmd == 0xAE);
            s2m->resp = 0;
            break;
        case 0xC1:
        case 0xC2:
            stock_matrix_power(m2s->cmd == 0xC1);
            s2m->resp = 0;
            break;
        default:
            break;
    }
}
