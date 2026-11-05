/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core.h"

enum { BATTERY_FRESH_MS = 10000, CRITICAL_HOLD_MS = 60000 };

static bool critical_reading(const struct gmk_power_inputs *inputs) {
    return inputs->battery_valid && inputs->battery_age_ms <= BATTERY_FRESH_MS && inputs->battery_percentage == 0 && !inputs->cable && !inputs->usb_powered;
}
bool gmk_power_core_critical_ready(const struct gmk_power_core *core, const struct gmk_power_inputs *inputs) {
    return core->critical && critical_reading(inputs) && (uint32_t)(inputs->now_ms - core->critical_since) >= CRITICAL_HOLD_MS;
}

void gmk_power_core_activity(struct gmk_power_core *core, bool active, uint32_t transition_ms) {
    if (active)
        core->inactive = false;
    else if (!core->inactive) {
        core->inactive       = true;
        core->inactive_since = transition_ms;
    }
}
enum gmk_power_request gmk_power_core_decide(struct gmk_power_core *core, const struct gmk_power_inputs *inputs) {
    if (!critical_reading(inputs))
        core->critical = false;
    else if (!core->critical) {
        core->critical       = true;
        core->critical_since = inputs->now_ms;
    }
    if (inputs->busy) return GMK_POWER_NONE;
    /* The existing QMK critical policy bypasses held keys after qualification.
     * The hardware coordinator must still clear reports and quiesce all work. */
    if (gmk_power_core_critical_ready(core, inputs)) return GMK_POWER_CRITICAL;
    if (!core->inactive || !inputs->timeout_ms || inputs->keys_held || (inputs->usb_powered && !inputs->usb_sleep_allowed)) return GMK_POWER_NONE;
    return (uint32_t)(inputs->now_ms - core->inactive_since) >= inputs->timeout_ms ? GMK_POWER_INACTIVITY : GMK_POWER_NONE;
}
