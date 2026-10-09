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

static volatile lpwr_state_t    lpwr_state          = LPWR_NORMAL;
static volatile lpwr_state_t    lpwr_previous_state = LPWR_NORMAL;
static volatile lpwr_prepare_t  lpwr_preparation    = LPWR_PREPARE_ORDINARY;
static lpwr_mode_t              lpwr_mode           = LPWR_MODE_TIMEOUT;
static uint32_t                 lpwr_timeout_value  = LPWR_TIMEOUT;
static uint32_t                 lpwr_timestamp      = 0x00;
static uint32_t                 lpwr_activity_timestamp;
static volatile lpwr_wakeupcd_t lpwr_wakeupcd  = LPWR_WAKEUP_NONE;
static volatile bool            manual_timeout = false;

static bool rgb_enable_bak = false;

void lpwr_state_changed_hook(void) __attribute__((weak));
void lpwr_state_changed_hook(void) {}

void last_matrix_activity_trigger(void);

void lpwr_clock_enable(void);
void lpwr_enter_stop(void);
void lpwr_exti_init(void);
void lpwr_stop_prepare(void);
bool lpwr_stop_pending(void);
void mcu_stop_mode(void);

extern void matrix_init_pins(void);

lpwr_state_t lpwr_get_state(void) {
    return lpwr_state;
}

lpwr_state_t lpwr_get_previous_state(void) {
    return lpwr_previous_state;
}

lpwr_prepare_t lpwr_get_prepare_class(void) {
    return lpwr_preparation;
}

void lpwr_set_prepare_class(lpwr_prepare_t preparation) {
    lpwr_preparation = preparation;
}

uint8_t lpwr_requested_classes_hook(void) __attribute__((weak));
uint8_t lpwr_requested_classes_hook(void) {
    return LPWR_PREPARE_ORDINARY;
}

void lpwr_set_state(lpwr_state_t state) {
    if (lpwr_state != state) split_diagnostics_event(SPLIT_DIAG_STATE, lpwr_state, state, manual_timeout);
    lpwr_previous_state = lpwr_state;
    lpwr_state          = state;
    manual_timeout      = false;
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
    lpwr_timestamp = timer_read32();
}

void lpwr_activity_trigger(void) {
    lpwr_activity_timestamp = timer_read32();
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

void lpwr_stop_prepare(void) __attribute__((weak));
void lpwr_stop_prepare(void) {}

bool lpwr_stop_pending(void) __attribute__((weak));
bool lpwr_stop_pending(void) {
    return lpwr_get_sleep_wakeupcd() != LPWR_WAKEUP_NONE;
}

void lpwr_enter_stop(void) {
    split_diagnostics_event(SPLIT_DIAG_STOP_ENTER, lpwr_state, manual_timeout, lpwr_wakeupcd);
    split_diagnostics_checkpoint(1); // Persist before clocks/wake IRQs change.
    chSysLock();
    // Clear stale IRQs before arming wake sources, never after a new edge.
    lpwr_stop_prepare();
    lpwr_exti_init();
    chSysUnlock();

    chSysDisable();
    // An IRQ can run when the arm lock is released. WB32's callback disables
    // EXTI after recording it; WFI then could sleep with no key/split wake IRQs.
    // A radio worker can also deliver host resume in this window.
    bool enter_stop = lpwr_get_state() == LPWR_STOP && !lpwr_stop_pending();
    if (enter_stop) mcu_stop_mode();
    lpwr_clock_enable();
    matrix_init_pins();
    chSysEnable();
    if (!enter_stop) split_diagnostics_event(SPLIT_DIAG_STOP_CANCEL, lpwr_state, manual_timeout, lpwr_wakeupcd);
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
        // V15 records suspend in every phase; the next state assignment clears it.
        manual_timeout = true;
    }
}

bool lpwr_is_allow_timeout_hook(void) __attribute__((weak));
bool lpwr_is_allow_timeout_hook(void) {
    return true;
}

bool lpwr_timeout_accept_hook(void) __attribute__((weak));
bool lpwr_timeout_accept_hook(void) {
    return true;
}

bool lpwr_is_allow_timeout(void) __attribute__((weak));
bool lpwr_is_allow_timeout(void) {
    uint32_t timeout = lpwr_timeout_value_read();

    if (lpwr_is_allow_timeout_hook() != true) {
        manual_timeout = false;
        return false;
    }

    // V15 preserves a pending manual request while initializing this clock.
    if (!lpwr_activity_timestamp) {
        lpwr_activity_trigger();
        return false;
    }

    if ((wireless_get_current_devs() == DEVS_USB) && (USB_DRIVER.state == USB_ACTIVE)) {
        // V15 refreshes ordinary USB activity; it still consumes manual requests.
        lpwr_activity_trigger();
    }

    if (manual_timeout || (timeout && (timer_elapsed32(lpwr_activity_timestamp) >= timeout))) {
        bool was_manual = manual_timeout;
        manual_timeout  = false;
        if (!lpwr_timeout_accept_hook()) return false;
#ifdef GMK70_SPLIT_DIAGNOSTICS
        split_diagnostics_sleep_accepted(was_manual, timer_elapsed32(lpwr_activity_timestamp), wireless_get_current_devs());
#else
        (void)was_manual;
#endif
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

    if (!delay || (timer_elapsed32(lpwr_timestamp_read()) >= delay)) {
        return true;
    }

    return false;
}

bool lpwr_is_allow_stop_hook(void) __attribute__((weak));
bool lpwr_is_allow_stop_hook(void) {
    return true;
}

void lpwr_usb_prepare_hook(void) __attribute__((weak));
void lpwr_usb_prepare_hook(void) {}

bool lpwr_is_allow_stop(void) __attribute__((weak));
bool lpwr_is_allow_stop(void) {
    uint32_t     delay    = LPWR_STOP_DELAY;
    lpwr_state_t previous = lpwr_get_previous_state();
    if (previous != LPWR_PRESLEEP && previous != LPWR_STOP) return false;

    // V15's first USB-special STOP resets the matrix/key counter immediately.
    // Repeated STOP returns retain the ordinary delay and class gate.
    if (lpwr_get_prepare_class() == LPWR_PREPARE_USB && lpwr_get_previous_state() == LPWR_PRESLEEP) {
        lpwr_usb_prepare_hook();
        return true;
    }

    if (lpwr_is_allow_stop_hook() != true) {
        return false;
    }

    if (!delay || (timer_elapsed32(lpwr_timestamp_read()) >= delay)) {
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

    if (!delay || (timer_elapsed32(lpwr_timestamp_read()) >= delay)) {
        return true;
    }

    return false;
}

void lpwr_presleep_hook(void) __attribute__((weak));
void lpwr_presleep_hook(void) {}

void lpwr_power_down_hook(void) __attribute__((weak));
void lpwr_power_down_hook(void) {
    suspend_power_down();
}

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
    lpwr_power_down_hook();
    lpwr_presleep_hook();
}

void lpwr_stop_hook_pre(void) __attribute__((weak));
void lpwr_stop_hook_pre(void) {}

void lpwr_stop_hook_post(void) __attribute__((weak));
void lpwr_stop_hook_post(void) {}

// V15 can remain in software STOP without entering MCU STOP after a mode change.
bool lpwr_can_enter_stop_hook(void) __attribute__((weak));
bool lpwr_can_enter_stop_hook(void) {
    return true;
}

void lpwr_stop_cb(void) __attribute__((weak));
void lpwr_stop_cb(void) {
    lpwr_stop_hook_pre();
    lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_NONE);
    if (lpwr_can_enter_stop_hook()) lpwr_enter_stop();

    // Preserve a host resume delivered by the radio worker during STOP entry.
    if (lpwr_get_state() == LPWR_STOP && lpwr_get_prepare_class() == LPWR_PREPARE_ORDINARY) {
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
    }

    if (lpwr_get_state() == LPWR_STOP) lpwr_stop_hook_post();
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

    split_diagnostics_checkpoint(2); // Save recovery once, before NORMAL clears the reason.

    last_matrix_activity_trigger();
}

void lpwr_task_hook_pre(void) __attribute__((weak));
void lpwr_task_hook_pre(void) {}

void lpwr_task(void) __attribute__((weak));
void lpwr_task(void) {
    lpwr_task_hook_pre();
    switch (lpwr_get_state()) {
        case LPWR_NORMAL: {
            bool timeout = lpwr_is_allow_timeout();
            if (timeout) {
                lpwr_set_state(LPWR_PRESLEEP);
                lpwr_set_prepare_class(LPWR_PREPARE_ORDINARY);
            }
            // All three stock predicates are evaluated, even after an earlier
            // request assigned PRE. Preserve their ordering and state hooks.
            uint8_t              requests  = lpwr_requested_classes_hook();
            const lpwr_prepare_t classes[] = {LPWR_PREPARE_CRITICAL, LPWR_PREPARE_USB};
            for (uint8_t i = 0; i < sizeof(classes) / sizeof(classes[0]); ++i) {
                if (!(requests & classes[i])) continue;
#ifdef GMK70_SPLIT_DIAGNOSTICS
                if (!timeout) split_diagnostics_sleep_accepted(true, timer_elapsed32(lpwr_activity_timestamp), wireless_get_current_devs());
#endif
                lpwr_set_state(LPWR_PRESLEEP);
                lpwr_set_prepare_class(classes[i]);
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
