/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum gmk_power_request { GMK_POWER_NONE, GMK_POWER_INACTIVITY, GMK_POWER_CRITICAL };
/* Zero initialization means active with no qualified critical battery reading. */
struct gmk_power_core {
    uint32_t inactive_since, critical_since;
    bool     inactive, critical;
};
struct gmk_power_inputs {
    uint32_t now_ms, timeout_ms, battery_age_ms;
    uint8_t  battery_percentage;
    bool     battery_valid, cable, usb_powered, usb_sleep_allowed, keys_held, busy;
};
/* An inactive transition supplies the logical time of the last native activity:
 * the adapter subtracts CONFIG_ZMK_IDLE_TIMEOUT from the native IDLE timestamp.
 * Repeated IDLE/SLEEP notifications must not restart this inactivity interval. */
void                   gmk_power_core_activity(struct gmk_power_core *core, bool active, uint32_t transition_ms);
enum gmk_power_request gmk_power_core_decide(struct gmk_power_core *core, const struct gmk_power_inputs *inputs);
/* Revalidate a qualified critical episode against a fresh current sample.
 * This never mutates admission history or grants general storage permission. */
bool gmk_power_core_critical_ready(const struct gmk_power_core *core, const struct gmk_power_inputs *inputs);
