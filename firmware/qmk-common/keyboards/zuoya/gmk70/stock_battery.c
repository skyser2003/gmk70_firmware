// Copyright 2024 yangzheng20003 (@yangzheng20003)
// SPDX-License-Identifier: GPL-2.0-or-later
#include "stock_battery.h"
#include "stock_keycodes.h"
#include "transactions.h"
#include "rgb_matrix.h"

enum {
    LEVEL_PRESS      = 0xA4,
    LEVEL_RELEASE    = 0xA5,
    CHARGING_PRESS   = 0xF0,
    CHARGING_RELEASE = 0xF1,
};

static bool level_requested;
static bool charging_requested;

void stock_battery_key(uint16_t keycode, bool pressed, bool wireless_mode) {
    uint8_t command;
    switch (keycode) {
        case HS_BATQ:
            // Stock applies this guard to both press and release.
            if (!wireless_mode) return;
            level_requested = pressed;
            command         = pressed ? LEVEL_PRESS : LEVEL_RELEASE;
            break;
        case KC_BATQ:
            charging_requested = pressed;
            command            = pressed ? CHARGING_PRESS : CHARGING_RELEASE;
            break;
        default:
            return;
    }

    const uint8_t request[5]  = {command, 0, 0, 0, 0};
    uint8_t       response[5] = {0};
    // One attempt per event, as in stock. No retry queue, timer, or expiry.
    (void)transaction_rpc_exec(USER_SYNC_MMS, sizeof(request), request, sizeof(response), response);
}

bool stock_battery_receive(uint8_t command, uint8_t *response) {
    switch (command) {
        case LEVEL_PRESS:
            level_requested = true;
            break;
        case LEVEL_RELEASE:
            level_requested = false;
            break;
        case CHARGING_PRESS:
            charging_requested = true;
            break;
        case CHARGING_RELEASE:
            charging_requested = false;
            break;
        default:
            return false;
    }
    // The stock F1 branch acknowledges with 1; A4/A5/F0 acknowledge with 0.
    *response = command == CHARGING_RELEASE ? 1 : 0;
    return true;
}

bool stock_battery_level_requested(void) {
    return level_requested;
}
bool stock_battery_charging_requested(void) {
    return charging_requested;
}

// Same number-row rendering as the original query(), using the stock shared
// percentage supplied by 0x55 instead of the receiving half's local radio.
void stock_battery_render(uint8_t percentage) {
    if (!level_requested) return;
    rgb_matrix_set_color_all(0, 0, 0);
    static const uint8_t indices[10] = RGB_MATRIX_BAT_INDEX_MAP;
    for (uint8_t i = 0; i < 10; ++i) {
        if (i < percentage / 10 || i == 0) {
            if (percentage >= 30) {
                rgb_matrix_set_color(indices[i], 0, 150, 0);
            } else {
                rgb_matrix_set_color(indices[i], 150, 0, 0);
            }
        } else {
            rgb_matrix_set_color(indices[i], 0, 0, 0);
        }
    }
}

void stock_battery_render_charging(bool cable_connected, bool fully_charged) {
    if (!charging_requested || !cable_connected) return;
    rgb_matrix_set_color(HS_MATRIX_BLINK_INDEX_BAT, fully_charged ? 0 : 80, fully_charged ? 80 : 0, 0);
}
