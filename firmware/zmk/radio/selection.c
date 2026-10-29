/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "selection.h"
#include "core.h"
#include <errno.h>

bool gmk_radio_selector_switch(struct gmk_radio_selector *selector, uint8_t raw, uint32_t now, uint8_t last_bt, uint8_t *out) {
    if (raw > 3) {
        gmk_radio_selector_cancel(selector);
        selector->have_candidate = false;
        return false;
    }
    if (!selector->have_candidate || selector->candidate_raw != raw) {
        gmk_radio_selector_cancel(selector);
        selector->have_candidate = true;
        selector->candidate_raw  = raw;
        selector->candidate_at   = now;
        return false;
    }
    if ((uint32_t)(now - selector->candidate_at) < 50) return false;
    if (selector->have_stable && selector->stable_raw == raw) return false;
    selector->have_stable = true;
    selector->stable_raw  = raw;
    switch (raw) {
        case 3:
            *out = GMK_HOST_USB;
            break;
        case 2:
            *out = last_bt >= GMK_HOST_BT1 && last_bt <= GMK_HOST_BT5 ? last_bt : GMK_HOST_BT1;
            break;
        case 1:
            *out = GMK_HOST_DONGLE;
            break;
        default:
            *out = UINT8_MAX;
            break;
    }
    return true;
}
int gmk_radio_selector_press(struct gmk_radio_selector *selector, uint8_t channel, uint32_t position, uint32_t now) {
    if (channel > GMK_HOST_DONGLE || position >= 72) return -EINVAL;
    selector->holding       = channel != GMK_HOST_USB;
    selector->held_channel  = channel;
    selector->held_position = (uint8_t)position;
    selector->held_at       = now;
    return 0;
}
void gmk_radio_selector_release(struct gmk_radio_selector *selector, uint32_t position) {
    if (selector->holding && selector->held_position == position) gmk_radio_selector_cancel(selector);
}
void gmk_radio_selector_cancel(struct gmk_radio_selector *selector) {
    selector->holding = false;
}
bool gmk_radio_selector_pair(struct gmk_radio_selector *selector, uint32_t now, uint8_t *out) {
    if (!selector->holding || (uint32_t)(now - selector->held_at) < 3000) return false;
    *out = selector->held_channel;
    gmk_radio_selector_cancel(selector);
    return true;
}
bool gmk_radio_selector_holding(const struct gmk_radio_selector *selector, uint32_t *position, uint8_t *channel) {
    if (!selector->holding) return false;
    *position = selector->held_position;
    *channel  = selector->held_channel;
    return true;
}
