// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

typedef enum {
    LPWR_NORMAL = 0,
    LPWR_PRESLEEP,
    LPWR_STOP,
    LPWR_WAKEUP,
} lpwr_state_t;

typedef enum {
    LPWR_PREPARE_ORDINARY = 0,
    LPWR_PREPARE_USB,
    LPWR_PREPARE_CRITICAL,
} lpwr_prepare_t;

typedef enum {
    LPWR_WAKEUP_NONE = 0,
    LPWR_WAKEUP_MATRIX,
    LPWR_WAKEUP_UART,
    LPWR_WAKEUP_CABLE,
    LPWR_WAKEUP_USB,
    LPWR_WAKEUP_ONEKEY,
    LPWR_WAKEUP_ENCODER,
    LPWR_WAKEUP_SWITCH,
} lpwr_wakeupcd_t;

typedef enum {
    LPWR_MODE_TIMEOUT = 0,
} lpwr_mode_t;

lpwr_state_t    lpwr_get_state(void);
lpwr_state_t    lpwr_get_previous_state(void);
lpwr_prepare_t  lpwr_get_prepare_class(void);
void            lpwr_set_prepare_class(lpwr_prepare_t preparation);
lpwr_mode_t     lpwr_get_mode(void);
uint32_t        lpwr_timestamp_read(void);
uint32_t        lpwr_timeout_value_read(void);
void            lpwr_set_timeout_value(uint32_t timeout);
void            lpwr_set_sleep_wakeupcd(lpwr_wakeupcd_t wakeupcd);
lpwr_wakeupcd_t lpwr_get_sleep_wakeupcd(void);
void            lpwr_update_timestamp(void);
void            lpwr_activity_trigger(void);
void            lpwr_set_timeout_manual(bool enable);
bool            lpwr_get_timeout_manual(void);
void            lpwr_set_state(lpwr_state_t state);
void            lpwr_set_mode(lpwr_mode_t mode);
void            lpwr_task(void);
void            lpwr_enable_wake_line(uint32_t pin, uint32_t edges);
void            lpwr_stop_hook_post(void);
