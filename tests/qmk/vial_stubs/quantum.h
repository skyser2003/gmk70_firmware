// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include_next "quantum.h"
#include "action.h"

#define LED_POWER_EN_PIN 5
#define LED_POWER_EN2_PIN 8
void gpio_write_pin(unsigned pin, bool enabled);
bool pre_process_record_user(uint16_t keycode, keyrecord_t *record);
