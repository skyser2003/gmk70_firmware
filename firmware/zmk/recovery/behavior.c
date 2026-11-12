/* SPDX-License-Identifier: GPL-2.0-or-later */
#define DT_DRV_COMPAT zuoya_gmk70_behavior_recovery
#include "recovery.h"
#include <errno.h>
#include <drivers/behavior.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zmk/behavior.h>
#include <zmk/events/position_state_changed.h>

static struct {
    uint32_t since_ms;
    uint32_t position;
    uint8_t  source;
    uint8_t  action;
} held;
static struct k_spinlock       held_lock;
static struct k_work_delayable settings_hold;

static void reset_settings(struct k_work *work) {
    ARG_UNUSED(work);
    k_spinlock_key_t key = k_spin_lock(&held_lock);
    bool             run = held.action == GMK_RECOVERY_SETTINGS_RESET;
    if (run) {
        uint32_t elapsed = k_uptime_get_32() - held.since_ms;
        if (elapsed < 3000U) {
            /* A previous hold's callback can already be queued when another
             * press reschedules this timer. Never consume the newer hold early. */
            (void)k_work_reschedule(&settings_hold, K_MSEC(3000U - elapsed));
            run = false;
        } else
            held.action = GMK_RECOVERY_NONE;
    }
    k_spin_unlock(&held_lock, key);
    if (run) (void)gmk_recovery_settings_reset();
}
static K_WORK_DELAYABLE_DEFINE(settings_hold, reset_settings);

static uint8_t source(struct zmk_behavior_binding_event event) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    return event.source;
#else
    ARG_UNUSED(event);
    return UINT8_MAX; /* Native local position-event source. */
#endif
}

int gmk_recovery_hold_press(enum gmk_recovery_action action, struct zmk_behavior_binding_event event) {
    if (action < GMK_RECOVERY_RESET || action > GMK_RECOVERY_SETTINGS_RESET) return -EINVAL;
    k_spinlock_key_t key = k_spin_lock(&held_lock);
    if (held.action != GMK_RECOVERY_NONE) {
        k_spin_unlock(&held_lock, key);
        return -EBUSY;
    }
    held.since_ms = action == GMK_RECOVERY_SETTINGS_RESET ? k_uptime_get_32() : (uint32_t)event.timestamp;
    held.position = event.position;
    held.source   = source(event);
    held.action   = (uint8_t)action;
    if (action == GMK_RECOVERY_SETTINGS_RESET) (void)k_work_reschedule(&settings_hold, K_MSEC(3000));
    k_spin_unlock(&held_lock, key);
    return 0;
}

static int release(uint32_t position, uint8_t source_id, int64_t timestamp) {
    k_spinlock_key_t key = k_spin_lock(&held_lock);
    if (held.action == GMK_RECOVERY_NONE || position != held.position || source_id != held.source) {
        k_spin_unlock(&held_lock, key);
        return 0;
    }
    enum gmk_recovery_action action = (enum gmk_recovery_action)held.action;
    held.action                     = GMK_RECOVERY_NONE;
    uint32_t elapsed                = (uint32_t)timestamp - held.since_ms;
    /* Clearing the action cancels the hold. A pending callback is harmless;
     * its elapsed-time check also protects a later press of the same key. */
    k_spin_unlock(&held_lock, key);
    if (action == GMK_RECOVERY_SETTINGS_RESET) return 0;
    if (elapsed < 3000U || elapsed > INT32_MAX) return 0;
    return gmk_recovery_request(action);
}

int gmk_recovery_release_event(struct zmk_behavior_binding_event event) {
    return release(event.position, source(event), event.timestamp);
}

static int position_released(const zmk_event_t *event) {
    const struct zmk_position_state_changed *position = as_zmk_position_state_changed(event);
    if (position && !position->state) (void)release(position->position, position->source, position->timestamp);
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(gmk_recovery_hold_release, position_released);
ZMK_SUBSCRIPTION(gmk_recovery_hold_release, zmk_position_state_changed);

#ifndef CONFIG_GMK70_MERGED_CONTROLS
static int pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    if (binding->param2 || binding->param1 < GMK_RECOVERY_RESET || binding->param1 > GMK_RECOVERY_SETTINGS_RESET) return -EINVAL;
    return gmk_recovery_hold_press((enum gmk_recovery_action)binding->param1, event);
}
static int released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    return gmk_recovery_release_event(event);
}
#    if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata values[] = {
    {.display_name = "Reset", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK_RECOVERY_RESET},
    {.display_name = "Bootloader", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK_RECOVERY_BOOTLOADER},
    {.display_name = "Clear app settings", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = GMK_RECOVERY_SETTINGS_RESET},
};
static const struct behavior_parameter_metadata_set sets[]   = {{.param1_values = values, .param1_values_len = ARRAY_SIZE(values)}};
static const struct behavior_parameter_metadata     metadata = {.sets_len = ARRAY_SIZE(sets), .sets = sets};
#    endif
static const struct behavior_driver_api api = {
    .binding_pressed  = pressed,
    .binding_released = released,
    /* Match native reset's role semantics. A settings reset on a peripheral is
     * rejected by the application's settings-owner/storage authorization. */
    .locality = BEHAVIOR_LOCALITY_EVENT_SOURCE,
#    if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#    endif
};
BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);

#endif /* legacy standalone behavior */
