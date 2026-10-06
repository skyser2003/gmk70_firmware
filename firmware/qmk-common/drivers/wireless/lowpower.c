// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "wireless.h"
#include "usb_main.h"
#include "split_diagnostics.h"

#ifndef LPWR_TIMEOUT
#    define LPWR_TIMEOUT 300000 // 5min
#endif

#ifndef LPWR_PRESLEEP_DELAY
#    define LPWR_PRESLEEP_DELAY 0
#endif

#ifndef LPWR_STOP_DELAY
#    define LPWR_STOP_DELAY 200
#endif

#ifndef LPWR_WAKEUP_DELAY
#    define LPWR_WAKEUP_DELAY 200
#endif

static lpwr_state_t    lpwr_state         = LPWR_NORMAL;
static lpwr_mode_t     lpwr_mode          = LPWR_MODE_TIMEOUT;
static uint32_t        lpwr_timeout_value = LPWR_TIMEOUT;
static uint32_t        lpwr_timestamp     = 0x00;
static uint32_t        lpwr_activity_timestamp;
static lpwr_wakeupcd_t lpwr_wakeupcd  = LPWR_WAKEUP_NONE;
static bool            manual_timeout = false;

static bool rgb_enable_bak = false;

void lpwr_state_changed_hook(void) __attribute__((weak));
void lpwr_state_changed_hook(void) {}

void last_matrix_activity_trigger(void);

void lpwr_clock_enable(void);
void lpwr_enter_stop(void);
void lpwr_exti_init(void);
void mcu_stop_mode(void);

extern void matrix_init_pins(void);

lpwr_state_t lpwr_get_state(void) {
    return lpwr_state;
}

void lpwr_set_state(lpwr_state_t state) {
    if (lpwr_state != state) split_diagnostics_event(SPLIT_DIAG_STATE, lpwr_state, state, manual_timeout);
    lpwr_state     = state;
    manual_timeout = false;
    lpwr_update_timestamp();
    lpwr_activity_trigger();
    lpwr_state_changed_hook();
}

lpwr_mode_t lpwr_get_mode(void) {
    return lpwr_mode;
}

void lpwr_set_mode(lpwr_mode_t mode) {
    lpwr_mode = mode;
}

void lpwr_set_timeout_value(uint32_t timeout) {
    lpwr_timeout_value = timeout;
}

uint32_t lpwr_timeout_value_read(void) {
    return lpwr_timeout_value;
}

void lpwr_update_timestamp(void) {
    lpwr_timestamp = sync_timer_read32();
}

void lpwr_activity_trigger(void) {
    lpwr_activity_timestamp = sync_timer_read32();
}

uint32_t lpwr_timestamp_read(void) {
    return lpwr_timestamp;
}

void lpwr_set_sleep_wakeupcd(lpwr_wakeupcd_t wakeupcd) {
    lpwr_wakeupcd = wakeupcd;
}

lpwr_wakeupcd_t lpwr_get_sleep_wakeupcd(void) {
    return lpwr_wakeupcd;
}

void lpwr_clock_enable(void) __attribute__((weak));
void lpwr_clock_enable(void) {}

void lpwr_exti_init(void) __attribute__((weak));
void lpwr_exti_init(void) {}

void mcu_stop_mode(void) __attribute__((weak));
void mcu_stop_mode(void) {}

void lpwr_enter_stop(void) {
    split_diagnostics_event(SPLIT_DIAG_STOP_ENTER, lpwr_state, manual_timeout, lpwr_wakeupcd);
    chSysLock();
    lpwr_exti_init();
    chSysUnlock();

    chSysDisable();
    mcu_stop_mode();
    lpwr_clock_enable();
    matrix_init_pins();
    chSysEnable();
    split_diagnostics_event(SPLIT_DIAG_STOP_EXIT, lpwr_state, manual_timeout, lpwr_wakeupcd);
}

void lpwr_set_timeout_manual(bool enable) {
    if (manual_timeout != enable) split_diagnostics_event(SPLIT_DIAG_MANUAL, enable, lpwr_state, 0);
    manual_timeout = enable;
}

bool lpwr_get_timeout_manual(void) {
    return manual_timeout;
}

// 2.4g mode, host state
bool lpwr_host_resume_allowed_hook(void) __attribute__((weak));
bool lpwr_host_resume_allowed_hook(void) {
    return true;
}

void md_receive_host_cb(bool resume) {
    split_diagnostics_event(SPLIT_DIAG_HOST, resume, lpwr_state, manual_timeout);
    if (resume) {
        if (lpwr_host_resume_allowed_hook() && (lpwr_get_state() == LPWR_PRESLEEP || lpwr_get_state() == LPWR_STOP)) {
            lpwr_update_timestamp();
            lpwr_set_state(LPWR_WAKEUP);
        }
    } else {
        if (lpwr_get_state() == LPWR_NORMAL) {
            manual_timeout = true;
        }
    }
}

bool lpwr_is_allow_timeout_hook(void) __attribute__((weak));
bool lpwr_is_allow_timeout_hook(void) {
    return true;
}

bool lpwr_is_allow_timeout(void) __attribute__((weak));
bool lpwr_is_allow_timeout(void) {
    uint32_t timeout = lpwr_timeout_value_read();

    if (lpwr_is_allow_timeout_hook() != true) {
        manual_timeout = false;
        return false;
    }

    if ((wireless_get_current_devs() == DEVS_USB) && (USB_DRIVER.state == USB_ACTIVE)) {
        manual_timeout = false;
        return false;
    }

    if (manual_timeout || (timeout && (sync_timer_elapsed32(lpwr_activity_timestamp) >= timeout))) {
#ifdef GMK70_SPLIT_DIAGNOSTICS
        const uint32_t idle_seconds = sync_timer_elapsed32(lpwr_activity_timestamp) / 1000;
        split_diagnostics_event(SPLIT_DIAG_TIMEOUT, manual_timeout, idle_seconds > UINT8_MAX ? UINT8_MAX : idle_seconds, wireless_get_current_devs());
#endif
        manual_timeout = false;
        return true;
    }

    return false;
}

bool lpwr_is_allow_presleep_hook(void) __attribute__((weak));
bool lpwr_is_allow_presleep_hook(void) {
    return true;
}

bool lpwr_is_allow_presleep(void) __attribute__((weak));
bool lpwr_is_allow_presleep(void) {
    uint32_t delay = LPWR_PRESLEEP_DELAY;

    if (lpwr_is_allow_presleep_hook() != true) {
        return false;
    }

    if (!delay || (sync_timer_elapsed32(lpwr_timestamp_read()) >= delay)) {
        return true;
    }

    return false;
}

bool lpwr_is_allow_stop_hook(void) __attribute__((weak));
bool lpwr_is_allow_stop_hook(void) {
    return true;
}

bool lpwr_is_allow_stop(void) __attribute__((weak));
bool lpwr_is_allow_stop(void) {
    uint32_t delay = LPWR_STOP_DELAY;

    if (lpwr_is_allow_stop_hook() != true) {
        return false;
    }

    if (!delay || (sync_timer_elapsed32(lpwr_timestamp_read()) >= delay)) {
        return true;
    }

    return false;
}

bool lpwr_is_allow_wakeup_hook(void) __attribute__((weak));
bool lpwr_is_allow_wakeup_hook(void) {
    return true;
}

bool lpwr_is_allow_wakeup(void) __attribute__((weak));
bool lpwr_is_allow_wakeup(void) {
    uint32_t delay = LPWR_WAKEUP_DELAY;

    if (lpwr_is_allow_wakeup_hook() != true) {
        return false;
    }

    if (!delay || (sync_timer_elapsed32(lpwr_timestamp_read()) >= delay)) {
        return true;
    }

    return false;
}

void lpwr_presleep_hook(void) __attribute__((weak));
void lpwr_presleep_hook(void) {}

void lpwr_presleep_cb(void) __attribute__((weak));
void lpwr_presleep_cb(void) {
#if defined(RGB_MATRIX_ENABLE)
    rgb_enable_bak = rgb_matrix_is_enabled();
    rgb_matrix_disable_noeeprom();
#elif defined(RGBLIGHT_ENABLE)
    rgb_enable_bak = rgblight_is_enabled();
    rgblight_disable_noeeprom();
#else
    rgb_enable_bak = false;
#endif
    suspend_power_down();
    lpwr_presleep_hook();
}

void lpwr_stop_hook_pre(void) __attribute__((weak));
void lpwr_stop_hook_pre(void) {}

void lpwr_stop_hook_post(void) __attribute__((weak));
void lpwr_stop_hook_post(void) {}

void lpwr_stop_cb(void) __attribute__((weak));
void lpwr_stop_cb(void) {
    lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_NONE);

    lpwr_stop_hook_pre();
    lpwr_enter_stop();

    switch (lpwr_get_sleep_wakeupcd()) {
        // Stock V13/V15 keep STOP when no accepted wake source was recorded.
        // A return from WFI alone must not restore RGB or reselect the radio.
        case LPWR_WAKEUP_NONE:
        case LPWR_WAKEUP_UART: {
            lpwr_set_state(LPWR_STOP);
        } break;
        default: {
            lpwr_set_state(LPWR_WAKEUP);
        } break;
    }

    lpwr_stop_hook_post();
}

void lpwr_wakeup_hook(void) __attribute__((weak));
void lpwr_wakeup_hook(void) {}

void lpwr_wakeup_cb(void) __attribute__((weak));
void lpwr_wakeup_cb(void) {
    if (rgb_enable_bak) {
#if defined(RGB_MATRIX_ENABLE)
        rgb_matrix_enable_noeeprom();
#elif defined(RGBLIGHT_ENABLE)
        rgblight_enable_noeeprom();
#endif
    }

    suspend_wakeup_init();
    lpwr_wakeup_hook();

    last_matrix_activity_trigger();
}

void lpwr_task(void) __attribute__((weak));
void lpwr_task(void) {
    switch (lpwr_get_state()) {
        case LPWR_NORMAL: {
            if (lpwr_is_allow_timeout()) {
                lpwr_update_timestamp();
                lpwr_set_state(LPWR_PRESLEEP);
            }
        } break;
        case LPWR_PRESLEEP: {
            if (lpwr_is_allow_presleep()) {
                lpwr_presleep_cb();
                lpwr_update_timestamp();
                lpwr_set_state(LPWR_STOP);
            }
        } break;
        case LPWR_STOP: {
            if (lpwr_is_allow_stop()) {
                lpwr_update_timestamp();
                lpwr_stop_cb();
            }
        } break;
        case LPWR_WAKEUP: {
            if (lpwr_is_allow_wakeup()) {
                lpwr_wakeup_cb();
                lpwr_update_timestamp();
                lpwr_set_state(LPWR_NORMAL);
            }
        } break;
        default:
            break;
    }
}
