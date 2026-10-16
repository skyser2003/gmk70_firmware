// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "quantum.h"
#include "usb_main.h"

typedef struct {
    uint8_t devs, last_btdevs;
} wake_board_config_t;
extern wake_board_config_t confinfo;

void clear_mods(void);
void clear_weak_mods(void);
void clear_keys(void);
void mousekey_clear(void);
void programmable_button_clear(void);
void host_system_send(uint16_t usage);
void host_consumer_send(uint16_t usage);
void led_wakeup(void);
void rgb_matrix_set_suspend_state(bool suspended);
void suspend_wakeup_init_modules(void);
void suspend_wakeup_init_quantum(void);
void suspend_wakeup_init_kb(void);
void suspend_power_down_quantum(void);
void gpio_write_pin_high(ioline_t pin);
