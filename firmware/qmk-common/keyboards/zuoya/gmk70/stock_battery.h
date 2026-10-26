// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Stock V13/V15 display requests are key events, separate from percentage sync.
void stock_battery_key(uint16_t keycode, bool pressed, bool wireless_mode);
bool stock_battery_receive(uint8_t command, uint8_t *response);
bool stock_battery_level_requested(void);
bool stock_battery_charging_requested(void);
void stock_battery_render(uint8_t percentage);
void stock_battery_render_charging(bool cable_connected, bool fully_charged);
