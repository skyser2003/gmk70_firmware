// SPDX-License-Identifier: GPL-2.0-or-later
#include "battery_indicator.h"
#include "config.h"

enum { SYNC_INTERVAL_MS = 250, RETRY_INTERVAL_MS = 50, PEER_TIMEOUT_MS = 1200 };

battery_indicator_state_t battery_indicator_state(bool level, bool charge, bool wireless, bool charging, bool full, uint8_t percent) {
    return (battery_indicator_state_t){
        .flags   = ((level && wireless) ? BATTERY_INDICATOR_LEVEL : 0) | ((charge && charging) ? BATTERY_INDICATOR_CHARGE : 0) | ((charge && charging && full) ? BATTERY_INDICATOR_FULL : 0),
        .percent = percent > 100 ? 100 : percent,
    };
}

bool battery_indicator_prepare(battery_indicator_sync_t *sync, battery_indicator_state_t state, uint32_t now, uint8_t body[4]) {
    if (sync->attempted && (uint32_t)(now - sync->attempted_at) < RETRY_INTERVAL_MS) return false;
    if (sync->acknowledged && sync->sent.flags == state.flags && sync->sent.percent == state.percent && (uint32_t)(now - sync->sent_at) < SYNC_INTERVAL_MS) return false;
    sync->attempted    = true;
    sync->attempted_at = now;
    body[0]            = 1; // Version, flags, percentage, reserved. Never copy C structs onto the wire.
    body[1]            = state.flags;
    body[2]            = state.percent;
    body[3]            = 0;
    return true;
}

void battery_indicator_acknowledge(battery_indicator_sync_t *sync, battery_indicator_state_t state, uint32_t now) {
    sync->acknowledged = true;
    sync->sent         = state;
    sync->sent_at      = now;
}

bool battery_indicator_receive(battery_indicator_sync_t *sync, const uint8_t body[4], uint32_t now) {
    if (body[0] != 1 || (body[1] & ~7) || body[2] > 100 || body[3] != 0 || ((body[1] & BATTERY_INDICATOR_FULL) && !(body[1] & BATTERY_INDICATOR_CHARGE))) return false;
    sync->peer        = (battery_indicator_state_t){.flags = body[1], .percent = body[2]};
    sync->received    = true;
    sync->received_at = now;
    return true;
}

battery_indicator_state_t battery_indicator_peer(const battery_indicator_sync_t *sync, uint32_t now) {
    if (!sync->received || (uint32_t)(now - sync->received_at) >= PEER_TIMEOUT_MS) return (battery_indicator_state_t){0};
    return sync->peer;
}

uint8_t battery_indicator_render(battery_indicator_state_t state, const battery_indicator_renderer_t *renderer) {
    if (state.flags & BATTERY_INDICATOR_LEVEL) {
        static const uint8_t indices[] = RGB_MATRIX_BAT_INDEX_MAP;
        renderer->set_all(0, 0, 0);
        for (uint8_t i = 0; i < sizeof(indices); ++i) {
            if (i == 0 || i < state.percent / 10) {
                renderer->set_color(indices[i], state.percent < 30 ? 150 : 0, state.percent >= 30 ? 150 : 0, 0);
            }
        }
        // Preserve the stock bar's priority if both requests are held.
        return BATTERY_INDICATOR_LEVEL;
    }
    if (state.flags & BATTERY_INDICATOR_CHARGE) {
        bool full = state.flags & BATTERY_INDICATOR_FULL;
        renderer->set_color(HS_MATRIX_BLINK_INDEX_BAT, full ? 0 : 80, full ? 80 : 0, 0);
        return BATTERY_INDICATOR_CHARGE;
    }
    return 0;
}
