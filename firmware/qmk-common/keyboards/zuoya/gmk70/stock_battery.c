// Copyright 2024 yangzheng20003 (@yangzheng20003)
// SPDX-License-Identifier: GPL-2.0-or-later
#include "stock_battery.h"
#include "stock_keycodes.h"
#include "transactions.h"
#include "rgb_matrix.h"
#include "battery_holds.h"

enum {
    LEVEL_PRESS      = 0xA4,
    LEVEL_RELEASE    = 0xA5,
    CHARGING_PRESS   = 0xF0,
    CHARGING_RELEASE = 0xF1,
};

enum { PHYSICAL_POSITIONS = 90, LEGACY_POSITION = 90 };
static struct gmk_battery_holds held;
static bool                     peer_requested[2];
static uint8_t                  sample_percentage;
static uint32_t                 sample_at;
static bool                     sample_valid;

static bool key_action(uint16_t keycode, enum gmk_battery_action *action) {
    switch (keycode) {
        case HS_BATQ:
            *action = GMK_BATTERY_ACTION_LEVEL;
            return true;
        case KC_BATQ:
            *action = GMK_BATTERY_ACTION_CHARGING;
            return true;
        default:
            return false;
    }
}

static void notify(enum gmk_battery_action action, bool previous) {
    bool active = gmk_battery_holds_active(&held, action);
    if (active == previous) return;
    uint8_t       command     = action == GMK_BATTERY_ACTION_LEVEL ? (active ? LEVEL_PRESS : LEVEL_RELEASE) : (active ? CHARGING_PRESS : CHARGING_RELEASE);
    const uint8_t request[5]  = {command, 0, 0, 0, 0};
    uint8_t       response[5] = {0};
    // Keep the stock command format and one attempt per aggregate transition.
    (void)transaction_rpc_exec(USER_SYNC_MMS, sizeof(request), request, sizeof(response), response);
}

static void set_key(uint16_t keycode, unsigned position, bool pressed, bool wireless_mode) {
    enum gmk_battery_action action;
    if (!key_action(keycode, &action) || (pressed && action == GMK_BATTERY_ACTION_LEVEL && !wireless_mode)) return;
    bool previous = gmk_battery_holds_active(&held, action);
    (void)gmk_battery_holds_set(&held, action, position, pressed);
    notify(action, previous);
}

void stock_battery_key(uint16_t keycode, bool pressed, bool wireless_mode) {
    // Synthetic/macro events have no physical matrix position.
    set_key(keycode, LEGACY_POSITION, pressed, wireless_mode);
}

void stock_battery_key_at(uint16_t keycode, uint8_t position, bool pressed, bool wireless_mode) {
    if (position >= PHYSICAL_POSITIONS) return;
    if (!pressed) {
        stock_battery_release_position(position);
        return;
    }
    set_key(keycode, position, true, wireless_mode);
}

void stock_battery_release_position(uint8_t position) {
    if (position >= PHYSICAL_POSITIONS) return;
    bool level    = gmk_battery_holds_active(&held, GMK_BATTERY_ACTION_LEVEL);
    bool charging = gmk_battery_holds_active(&held, GMK_BATTERY_ACTION_CHARGING);
    if (!gmk_battery_holds_release(&held, position)) return;
    notify(GMK_BATTERY_ACTION_LEVEL, level);
    notify(GMK_BATTERY_ACTION_CHARGING, charging);
}

void stock_battery_sample(uint8_t percentage, uint32_t now) {
    sample_percentage = percentage;
    sample_at         = now;
    sample_valid      = percentage <= 100;
}

uint8_t stock_battery_sample_read(uint32_t now) {
    return sample_valid && (uint32_t)(now - sample_at) <= 10000 ? sample_percentage : UINT8_MAX;
}

bool stock_battery_receive(uint8_t command, uint8_t *response) {
    switch (command) {
        case LEVEL_PRESS:
            peer_requested[GMK_BATTERY_ACTION_LEVEL] = true;
            break;
        case LEVEL_RELEASE:
            peer_requested[GMK_BATTERY_ACTION_LEVEL] = false;
            break;
        case CHARGING_PRESS:
            peer_requested[GMK_BATTERY_ACTION_CHARGING] = true;
            break;
        case CHARGING_RELEASE:
            peer_requested[GMK_BATTERY_ACTION_CHARGING] = false;
            break;
        default:
            return false;
    }
    // The stock F1 branch acknowledges with 1; A4/A5/F0 acknowledge with 0.
    *response = command == CHARGING_RELEASE ? 1 : 0;
    return true;
}

bool stock_battery_level_requested(void) {
    return peer_requested[GMK_BATTERY_ACTION_LEVEL] || gmk_battery_holds_active(&held, GMK_BATTERY_ACTION_LEVEL);
}
bool stock_battery_charging_requested(void) {
    return peer_requested[GMK_BATTERY_ACTION_CHARGING] || gmk_battery_holds_active(&held, GMK_BATTERY_ACTION_CHARGING);
}

// Same number-row rendering as the original query(), using the stock shared
// percentage supplied by 0x55 instead of the receiving half's local radio.
void stock_battery_render(uint8_t percentage) {
    if (!stock_battery_level_requested()) return;
    rgb_matrix_set_color_all(0, 0, 0);
    static const uint8_t indices[10] = RGB_MATRIX_BAT_INDEX_MAP;
    for (uint8_t i = 0; i < 10; ++i) {
        if (percentage > 100) {
            rgb_matrix_set_color(indices[i], 24, 0, 24);
        } else if (i < percentage / 10 || i == 0) {
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
    if (!stock_battery_charging_requested() || !cable_connected) return;
    rgb_matrix_set_color(HS_MATRIX_BLINK_INDEX_BAT, fully_charged ? 0 : 80, fully_charged ? 80 : 0, 0);
}
