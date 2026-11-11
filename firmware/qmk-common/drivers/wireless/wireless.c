
// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "wireless.h"
#include "split_diagnostics.h"
#include "radio_keys.h"
#include "smsg.h"

#ifndef WLS_INQUIRY_BAT_TIME
#    define WLS_INQUIRY_BAT_TIME 3000
#endif

static uint8_t  wls_devs = DEVS_USB;
static uint8_t  reconnect_attempts, reconnect_state;
static uint32_t reconnect_at, reconnect_generation;

static void reconnect_start(bool pair) {
    reconnect_generation = md_connection_generation(NULL);
    reconnect_state      = MD_STATE_DISCONNECTED;
    reconnect_at         = sync_timer_read32();
    // The caller sends the first SELECT. Two additional status refreshes are
    // enough to recover a lost CONNECTED without retrying forever or pairing.
    reconnect_attempts = wls_devs != DEVS_USB && !pair ? 2 : 0;
}

void wireless_wake(void) {
    if (!is_keyboard_master() || wls_devs == DEVS_USB) return;
    reconnect_start(false);
    md_devs_change(wls_devs, false);
}

static void reconnect_task(void) {
    if (!is_keyboard_master() || wls_devs == DEVS_USB) return;
    uint8_t  state;
    uint32_t generation = md_connection_generation(&state);
    if (generation != reconnect_generation) {
        reconnect_generation = generation;
        if (state == MD_STATE_DISCONNECTED) {
            if (reconnect_state != state) {
                reconnect_attempts = 3;
                reconnect_at       = sync_timer_read32();
            }
        } else {
            reconnect_attempts = 0;
        }
        reconnect_state = state;
    }
    // Maintenance never wakes STOP/PRE, resets the idle timer, or occupies the
    // queue behind an outstanding control. ACK is not a connection status.
    if (!reconnect_attempts || lpwr_get_state() != LPWR_NORMAL || smsg_is_busy() || sync_timer_elapsed32(reconnect_at) < 5000) return;
    --reconnect_attempts;
    reconnect_at = sync_timer_read32();
    md_devs_change(wls_devs, false);
}

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
    reconnect_attempts = 0;
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

    if (md_reports_inhibited()) return;

    if (report != NULL) {
        wls_report_kb[0] = report->mods;
        memcpy(&wls_report_kb[2], report->keys, KEYBOARD_REPORT_KEYS);
    }
    md_send_kb(wls_report_kb);
}

void wireless_send_nkro(report_nkro_t *report) {
    static struct gmk70_radio_keys keys;
    uint8_t                        keyboard_report[MD_SND_CMD_KB_LEN];
    uint8_t                        overflow_report[MD_SND_CMD_NKRO_LEN];

    if (md_reports_inhibited()) return;

    gmk70_radio_keys_encode(&keys, report ? report->mods : 0, report ? report->bits : NULL, report ? sizeof(report->bits) : 0, keyboard_report, overflow_report);
    md_send_kb(keyboard_report);
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

    if (md_reports_inhibited()) return;

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
    if (md_reports_inhibited()) return;

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

void wireless_reports_reset_kb(void) __attribute__((weak));
void wireless_reports_reset_kb(void) {}

void wireless_process_connection_changes(void) {
    if (md_take_report_reset() && is_keyboard_master() && wls_devs != DEVS_USB) {
        wireless_reports_reset_kb();
        // Pairing can suppress releases after native caches already remember
        // them. Force empty wire reports when delivery becomes available again.
        wls_transport_enable(false);
    }
}

void wireless_devs_change(uint8_t old_devs, uint8_t new_devs, bool reset) {
    if (old_devs != new_devs || reset) split_diagnostics_event(SPLIT_DIAG_DEVICE, old_devs, new_devs, reset);
    // Select the host driver before the asynchronous radio connection completes.
    // Otherwise the first connection leaves reports routed to USB indefinitely.
    if (is_keyboard_master()) {
        // Discard native and custom ownership before selecting a new host.
        // Same-channel wake keeps its held inputs and driver unchanged.
        if (wls_devs != new_devs || reset) wireless_reports_reset_kb();
        // Wireless channel changes keep the same driver, so set_transport()
        // cannot release their outgoing host. Preserve same-channel wake keys.
        if (get_transport() == TRANSPORT_WLS && new_devs != DEVS_USB && (wls_devs != new_devs || reset)) {
            wls_transport_enable(false);
        }
        set_transport(new_devs == DEVS_USB ? TRANSPORT_USB : TRANSPORT_WLS);
    }

    if ((wls_devs != new_devs) || reset) {
        *md_getp_state()     = MD_STATE_DISCONNECTED;
        *md_getp_indicator() = 0;
    }

    // V15 wakes PRE/STOP even when the peer reselects the same device.
    // The independent USB/critical request latches are retained.
    if (lpwr_get_state() == LPWR_PRESLEEP || lpwr_get_state() == LPWR_STOP) {
        lpwr_set_state(LPWR_WAKEUP);
        lpwr_set_prepare_class(LPWR_PREPARE_ORDINARY);
    }

    wls_devs = new_devs;
    last_matrix_activity_trigger();
    lpwr_activity_trigger();

    reconnect_start(reset);
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
    reconnect_task();
#ifndef GMK70_RADIO_THREAD
    md_main_task();
#endif
    wireless_process_connection_changes();
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
