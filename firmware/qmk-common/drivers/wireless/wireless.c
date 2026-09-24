
// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "wireless.h"

#ifndef WLS_INQUIRY_BAT_TIME
#    define WLS_INQUIRY_BAT_TIME 3000
#endif

static uint8_t wls_devs = DEVS_USB;

void last_matrix_activity_trigger(void);

uint8_t wireless_keyboard_leds(void);
void    wireless_send_keyboard(report_keyboard_t *report);
void    wireless_send_nkro(report_nkro_t *report);
void    wireless_send_mouse(report_mouse_t *report);
void    wireless_send_extra(report_extra_t *report);

host_driver_t wireless_driver = {
    .keyboard_leds = wireless_keyboard_leds,
    .send_keyboard = wireless_send_keyboard,
    .send_nkro     = wireless_send_nkro,
    .send_mouse    = wireless_send_mouse,
    .send_extra    = wireless_send_extra,
};

void wireless_init(void) {
    md_init();
}

uint8_t wireless_keyboard_leds(void) {
    if (*md_getp_state() == MD_STATE_CONNECTED) {
        return *md_getp_indicator();
    }

    return 0;
}

void wireless_send_keyboard(report_keyboard_t *report) {
    uint8_t wls_report_kb[MD_SND_CMD_KB_LEN] = {0};

    if (*md_getp_state() != MD_STATE_CONNECTED) {
        wireless_devs_change(wls_devs, wls_devs, false);
        return;
    }

    if (report != NULL) {
        wls_report_kb[0] = report->mods;
        memcpy(&wls_report_kb[2], report->keys, KEYBOARD_REPORT_KEYS);
    }
    md_send_kb(wls_report_kb);
}

void wireless_send_nkro(report_nkro_t *report) {
    static report_keyboard_t keyboard_report                      = {0};
    uint8_t                  overflow_report[MD_SND_CMD_NKRO_LEN] = {0};

    if (*md_getp_state() != MD_STATE_CONNECTED) {
        wireless_devs_change(wls_devs, wls_devs, false);
        return;
    }

    if (report == NULL) {
        memset(&keyboard_report, 0, sizeof(keyboard_report));
    } else {
        keyboard_report.mods = report->mods;
        // Keep held keys in their existing slots; holes must not end the scan.
        for (uint8_t slot = 0; slot < KEYBOARD_REPORT_KEYS; slot++) {
            uint8_t key = keyboard_report.keys[slot];
            if (key / 8 >= NKRO_REPORT_BITS || !(report->bits[key / 8] & (1u << (key % 8)))) {
                keyboard_report.keys[slot] = 0;
            }
        }
        for (uint16_t key = 1; key < NKRO_REPORT_BITS * 8; key++) {
            if (!(report->bits[key / 8] & (1u << (key % 8)))) {
                continue;
            }
            uint8_t empty_slot = KEYBOARD_REPORT_KEYS;
            bool    present    = false;
            for (uint8_t slot = 0; slot < KEYBOARD_REPORT_KEYS; slot++) {
                if (keyboard_report.keys[slot] == key) {
                    present = true;
                    break;
                }
                if (!keyboard_report.keys[slot] && empty_slot == KEYBOARD_REPORT_KEYS) {
                    empty_slot = slot;
                }
            }
            if (present) {
                continue;
            }
            if (empty_slot < KEYBOARD_REPORT_KEYS) {
                keyboard_report.keys[empty_slot] = (uint8_t)key;
            } else if (key < sizeof(overflow_report) * 8) {
                overflow_report[key / 8] |= 1u << (key % 8);
            }
        }
    }
    wireless_driver.send_keyboard(&keyboard_report);
    md_send_nkro(overflow_report);
}

void wireless_send_mouse(report_mouse_t *report) {
    typedef struct {
        uint8_t buttons;
        int8_t  x;
        int8_t  y;
        int8_t  z;
        int8_t  h;
    } __attribute__((packed)) wls_report_mouse_t;

    wls_report_mouse_t wls_report_mouse = {0};

    if (*md_getp_state() != MD_STATE_CONNECTED) {
        wireless_devs_change(wls_devs, wls_devs, false);
        return;
    }

    if (report != NULL) {
        wls_report_mouse.buttons = report->buttons;
        wls_report_mouse.x       = report->x;
        wls_report_mouse.y       = report->y;
        wls_report_mouse.z       = report->h;
        wls_report_mouse.h       = report->v;
    }

    md_send_mouse((uint8_t *)&wls_report_mouse);
}

void wireless_send_extra(report_extra_t *report) {
    if (*md_getp_state() != MD_STATE_CONNECTED) {
        wireless_devs_change(wls_devs, wls_devs, false);
        return;
    }

    if (report != NULL) {
        uint16_t usage = report->usage;
        switch (report->report_id) {
            case REPORT_ID_SYSTEM: {
                usage = (usage >= 0x81 && usage <= 0x83) ? (0x01 << (usage - 0x81)) : 0;
                md_send_system((uint8_t *)&usage);
            } break;
            case REPORT_ID_CONSUMER: {
                md_send_consumer((uint8_t *)&usage);
            } break;
            default:
                break;
        }
    }
}

void wireless_devs_change_user(uint8_t old_devs, uint8_t new_devs, bool reset) __attribute__((weak));
void wireless_devs_change_user(uint8_t old_devs, uint8_t new_devs, bool reset) {}

void wireless_devs_change_kb(uint8_t old_devs, uint8_t new_devs, bool reset) __attribute__((weak));
void wireless_devs_change_kb(uint8_t old_devs, uint8_t new_devs, bool reset) {}

void wireless_devs_change(uint8_t old_devs, uint8_t new_devs, bool reset) {
    // Select the host driver before the asynchronous radio connection completes.
    // Otherwise the first connection leaves reports routed to USB indefinitely.
    if (is_keyboard_master()) {
        set_transport(new_devs == DEVS_USB ? TRANSPORT_USB : TRANSPORT_WLS);
    }

    if ((wls_devs != new_devs) || reset) {
        *md_getp_state()     = MD_STATE_DISCONNECTED;
        *md_getp_indicator() = 0;
    }

    wls_devs = new_devs;
    last_matrix_activity_trigger();

    md_devs_change(new_devs, reset);
    wireless_devs_change_kb(old_devs, new_devs, reset);
    wireless_devs_change_user(old_devs, new_devs, reset);
}

uint8_t wireless_get_current_devs(void) {
    return wls_devs;
}

void wireless_pre_task(void) __attribute__((weak));
void wireless_pre_task(void) {}

void wireless_post_task(void) __attribute__((weak));
void wireless_post_task(void) {}

void wireless_task(void) {
    wireless_pre_task();
    lpwr_task();
    md_main_task();
    wireless_post_task();

    /* usb_remote_wakeup() should be invoked last so that we have chance
     * to switch to wireless after start-up when usb is not connected
     */
    if (get_transport() == TRANSPORT_USB) {
        usb_remote_wakeup();
    } else if (lpwr_get_state() == LPWR_NORMAL) {
        static uint32_t inqtimer = 0x00;

        if (sync_timer_elapsed32(inqtimer) >= (WLS_INQUIRY_BAT_TIME)) {
            if (md_inquire_bat()) {
                inqtimer = sync_timer_read32();
            }
        }
    }
}

void housekeeping_task_kb(void) {
    wireless_task();
}
