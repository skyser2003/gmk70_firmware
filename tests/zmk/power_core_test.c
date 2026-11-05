/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core.h"
#include <assert.h>
#include <stdio.h>

static struct gmk_power_inputs inputs(void) {
    return (struct gmk_power_inputs){.timeout_ms = 60000, .battery_valid = true, .battery_percentage = 50};
}
static void native_activity_owns_inactivity_and_repeated_sleep_preserves_its_start(void) {
    struct gmk_power_core   core = {0};
    struct gmk_power_inputs in   = inputs();
    in.now_ms                    = 1000000;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
    gmk_power_core_activity(&core, false, 1000);
    in.now_ms = 60999;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
    gmk_power_core_activity(&core, false, 60000);
    in.now_ms = 61000;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_INACTIVITY);
    gmk_power_core_activity(&core, true, 61000);
    in.now_ms = 999999;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
}
static void disabled_sleep_held_keys_busy_work_and_usb_power_gate_inactivity(void) {
    struct gmk_power_core   core = {0};
    struct gmk_power_inputs in   = inputs();
    gmk_power_core_activity(&core, false, 0);
    in.now_ms     = 60000;
    in.timeout_ms = 0;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
    in.timeout_ms = 60000;
    in.keys_held  = true;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
    in.keys_held = false;
    in.busy      = true;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
    in.busy        = false;
    in.usb_powered = true;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
    in.usb_sleep_allowed = true;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_INACTIVITY);
}
static void critical_requires_fresh_zero_for_sixty_seconds_and_bypasses_held_keys(void) {
    struct gmk_power_core   core = {0};
    struct gmk_power_inputs in   = inputs();
    in.battery_percentage        = 0;
    in.timeout_ms                = 0;
    in.keys_held                 = true;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
    in.now_ms = 59999;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
    in.now_ms = 60000;
    in.busy   = true;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
    in.busy = false;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_CRITICAL);
}
static void unknown_stale_nonzero_and_external_power_reset_critical_qualification(void) {
    for (unsigned reset = 0; reset < 6; ++reset) {
        struct gmk_power_core   core = {0};
        struct gmk_power_inputs in   = inputs();
        in.battery_percentage        = 0;
        assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
        in.now_ms = 59999;
        if (reset == 0) in.battery_valid = false;
        if (reset == 1) in.battery_age_ms = 10001;
        if (reset == 2) in.battery_percentage = 1;
        if (reset == 3) in.cable = true;
        if (reset == 4) in.usb_powered = true;
        if (reset == 5) in.battery_percentage = 101;
        assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
        in                    = inputs();
        in.battery_percentage = 0;
        in.now_ms             = 60000;
        assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
        in.now_ms = 119999;
        assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
        in.now_ms = 120000;
        assert(gmk_power_core_decide(&core, &in) == GMK_POWER_CRITICAL);
    }
}
static void stale_readings_cannot_shutdown_and_boundary_age_remains_fresh(void) {
    struct gmk_power_core   core = {0};
    struct gmk_power_inputs in   = inputs();
    in.battery_percentage        = 0;
    in.battery_age_ms            = UINT32_MAX;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
    in.now_ms = 1000000;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
    in.battery_age_ms = 10000;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
    in.now_ms = 1060000;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_CRITICAL);
}
static void uptime_wrap_preserves_native_and_critical_deadlines(void) {
    struct gmk_power_core   core = {0};
    struct gmk_power_inputs in   = inputs();
    gmk_power_core_activity(&core, false, UINT32_MAX - 1000);
    in.now_ms = 58998;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
    in.now_ms = 58999;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_INACTIVITY);
    core                  = (struct gmk_power_core){0};
    in.battery_percentage = 0;
    in.now_ms             = UINT32_MAX - 1000;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
    in.now_ms = 58998;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_NONE);
    in.now_ms = 58999;
    assert(gmk_power_core_decide(&core, &in) == GMK_POWER_CRITICAL);
}
int main(void) {
    native_activity_owns_inactivity_and_repeated_sleep_preserves_its_start();
    disabled_sleep_held_keys_busy_work_and_usb_power_gate_inactivity();
    critical_requires_fresh_zero_for_sixty_seconds_and_bypasses_held_keys();
    unknown_stale_nonzero_and_external_power_reset_critical_qualification();
    stale_readings_cannot_shutdown_and_boundary_age_remains_fresh();
    uptime_wrap_preserves_native_and_critical_deadlines();
    puts("Power policy: native activity timestamps, sleep gates, fresh critical qualification and wrap passed");
    return 0;
}
