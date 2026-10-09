#include "wls.h"
#include "split_diagnostics.h"

bool hs_modeio_detection(bool update, uint8_t *mode, uint8_t lsat_btdev) {
    static uint32_t scan_timer = 0x00;

    if ((update != true) && (timer_elapsed32(scan_timer) <= (HS_MODEIO_DETECTION_TIME))) {
        return false;
    }
    scan_timer = timer_read32();
#if defined(HS_BT_DEF_PIN) && defined(HS_2G4_DEF_PIN)
    uint8_t        now_mode  = 0x00;
    uint8_t        hs_mode   = 0x00;
    static uint8_t last_mode = 0x00;
    bool           sw_mode   = false;
    now_mode                 = (HS_GET_MODE_PIN(HS_USB_PIN_STATE) ? 3 : (HS_GET_MODE_PIN(HS_BT_PIN_STATE) ? 1 : ((HS_GET_MODE_PIN(HS_2G4_PIN_STATE) ? 2 : 0))));
    hs_mode                  = (*mode >= DEVS_BT1 && *mode <= DEVS_BT5) ? 1 : ((*mode == DEVS_2G4) ? 2 : ((*mode == DEVS_USB) ? 3 : 0));
    sw_mode                  = ((update || (last_mode == now_mode)) && (hs_mode != now_mode)) ? true : false;
    last_mode                = now_mode;

    switch (now_mode) {
        case 1:
            *mode = hs_bt;
            if (sw_mode) {
                wireless_devs_change(wireless_get_current_devs(), lsat_btdev, false);
            }
            break;
        case 2:
            *mode = hs_2g4;
            if (sw_mode) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_2G4, false);
            }
            break;
        case 3:
            *mode = hs_usb;
            if (sw_mode) wireless_devs_change(wireless_get_current_devs(), DEVS_USB, false);

            break;
        default:
            break;
    }

    if (sw_mode) {
        hs_rgb_blink_set_timer(timer_read32());
        suspend_wakeup_init();
        return true;
    }
#else
    *mode = hs_none;
#endif

    return false;
}

static uint32_t hs_linker_rgb_timer = 0x00;

bool hs_mode_scan(bool update, uint8_t moude, uint8_t lsat_btdev) {
    if (hs_modeio_detection(update, &moude, lsat_btdev)) {
        return true;
    }
    hs_rgb_blink_hook();
    return false;
}

void hs_rgb_blink_set_timer(uint32_t time) {
    hs_linker_rgb_timer = time;
}

uint32_t hs_rgb_blink_get_timer(void) {
    return hs_linker_rgb_timer;
}

bool hs_rgb_blink_hook() {
    static uint8_t last_status;

    if (!is_keyboard_master()) {
        return false;
    }

    if (last_status != *md_getp_state()) {
        last_status = *md_getp_state();
        hs_rgb_blink_set_timer(0x00);
    }

    switch (*md_getp_state()) {
        case MD_STATE_NONE: {
            hs_rgb_blink_set_timer(0x00);
        } break;

        case MD_STATE_DISCONNECTED:
            if (hs_rgb_blink_get_timer() == 0x00) {
                hs_rgb_blink_set_timer(timer_read32());
                extern void wireless_devs_change_kb(uint8_t old_devs, uint8_t new_devs, bool reset);
                wireless_devs_change_kb(wireless_get_current_devs(), wireless_get_current_devs(), false);
            } else {
                if (timer_elapsed32(hs_rgb_blink_get_timer()) >= HS_LBACK_TIMEOUT) {
                    hs_rgb_blink_set_timer(timer_read32());
                    md_send_devctrl(MD_SND_CMD_DEVCTRL_USB);
                    wait_ms(200);
                    split_diagnostics_event(SPLIT_DIAG_SLEEP_REASON, SPLIT_DIAG_RADIO_DISCONNECTED, *md_getp_state(), 0);
                    lpwr_set_timeout_manual(true);
                }
            }
            break;
        case MD_STATE_CONNECTED:
        default:
            break;
    }
    return true;
}

void lpwr_exti_init_hook(void) {
    // V15 retains the boot/encoder GPIO modes for these inputs.
    lpwr_enable_wake_line(HS_BAT_CABLE_PIN, PAL_EVENT_MODE_RISING_EDGE);
    if (lpwr_get_prepare_class() == LPWR_PREPARE_ORDINARY) {
        lpwr_enable_wake_line(LPWR_EXTRA_WAKE_PIN, PAL_EVENT_MODE_BOTH_EDGES);
    }
#ifdef HS_BT_DEF_PIN
    if (is_keyboard_master()) lpwr_enable_wake_line(HS_BT_DEF_PIN, PAL_EVENT_MODE_BOTH_EDGES);
#endif
#ifdef HS_2G4_DEF_PIN
    if (is_keyboard_master()) lpwr_enable_wake_line(HS_2G4_DEF_PIN, PAL_EVENT_MODE_BOTH_EDGES);
#endif
}

void lpwr_exti_init_post_hook(void) {
    // Stock configures split RX last, as floating input on both edges.
    gpio_set_pin_input(SERIAL_USART_RX_PIN);
    waitInputPinDelay();
    lpwr_enable_wake_line(SERIAL_USART_RX_PIN, PAL_EVENT_MODE_BOTH_EDGES);
}

bool lpwr_matrix_wake_allowed_hook(void) {
    // Critical class arms no matrix IRQs. USB special retains one row/column.
    return lpwr_get_prepare_class() != LPWR_PREPARE_CRITICAL;
}

bool lpwr_matrix_pin_wake_allowed_hook(uint32_t pin) {
    // V15 tests its left pin table even when selecting the right column table.
    // The C3 column index therefore selects C7 on a right-side non-master.
    return lpwr_get_prepare_class() != LPWR_PREPARE_USB || pin == C3 || pin == C13;
}

void palcallback_cb(uint8_t line) {
    switch (line) {
        case PAL_PAD(C13): {
            lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_ONEKEY);
        } break;
        case PAL_PAD(HS_BAT_CABLE_PIN): {
            lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_CABLE);
        } break;
#ifdef HS_BT_DEF_PIN
        case PAL_PAD(HS_2G4_DEF_PIN): {
            lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_SWITCH);
        } break;
#endif

#ifdef HS_2G4_DEF_PIN
        case PAL_PAD(HS_BT_DEF_PIN): {
            lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_SWITCH);
        } break;
#endif
        default: {
        } break;
    }
}

void lpwr_stop_hook_pre(void) {
    gpio_write_pin_low(LED_POWER_EN_PIN);
    gpio_write_pin_low(LED_POWER_EN2_PIN);
    // V15 leaves the radio alone in USB-with-cable mode. Otherwise it drains
    // the USB-selection command before STOP, and waits 300 ms if disconnected.
    if (wireless_get_current_devs() == DEVS_USB && gmk70_stock_cable_flag()) return;
    bool special = lpwr_get_prepare_class() != LPWR_PREPARE_ORDINARY;
    if ((special && lpwr_get_previous_state() != LPWR_STOP) || (!special && *md_getp_state() != MD_STATE_CONNECTED)) {
        md_send_devctrl(MD_SND_CMD_DEVCTRL_USB);
        md_wait_for_commands();
        if (!special) wait_ms(300);
    }
}

void lpwr_stop_hook_post(void) {
    lpwr_prepare_t preparation = lpwr_get_prepare_class();
    if (preparation != LPWR_PREPARE_ORDINARY) {
        switch (lpwr_get_sleep_wakeupcd()) {
            case LPWR_WAKEUP_USB:
            case LPWR_WAKEUP_CABLE: {
                // One state assignment, before clearing the raw request latch.
                lpwr_set_state(LPWR_WAKEUP);
                lpwr_set_prepare_class(LPWR_PREPARE_ORDINARY);
                if (preparation == LPWR_PREPARE_CRITICAL)
                    lower_sleep = false;
                else
                    gmk70_stock_usb_sleep_set(false);
            } break;
            default: {
                lpwr_set_state(LPWR_STOP);
            } break;
        }
    }
}
