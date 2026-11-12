/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum gmk_recovery_action {
    GMK_RECOVERY_NONE           = 0,
    GMK_RECOVERY_RESET          = 1,
    GMK_RECOVERY_BOOTLOADER     = 2,
    GMK_RECOVERY_SETTINGS_RESET = 3,
};

/* Thread context only. Reset settings delegates to native zmk_settings_erase,
 * which the application must wrap with its owned-bank storage operation. A
 * failed erase returns its error and never reboots. Successful actions reboot. */
int gmk_recovery_request(enum gmk_recovery_action action);
/* Physical settings key: central settings owner, any output, no reboot. */
int gmk_recovery_settings_reset(void);

/* Zero-initialize once at startup. Caller owns this small state; no timer,
 * thread, storage access or reset occurs while sampling physical switches. */
struct gmk_recovery_startup {
    uint32_t                 started_at;
    enum gmk_recovery_action candidate;
    bool                     sampled;
    bool                     finished;
};

/* Chords must begin within 250 ms of the first sample and remain continuously
 * held for 3 seconds. Settings chord takes precedence. Any subsequent change
 * disarms recovery for this boot. Dispatch a returned action from a safe worker. */
enum gmk_recovery_action gmk_recovery_startup_sample(struct gmk_recovery_startup *state, bool bootloader_chord, bool settings_chord, uint32_t now_ms);
/* Scanner-only local LEFT rows. Dispatch occurs once, after releasing all keys,
 * on the system workqueue so the separate settings worker remains available. */
void gmk_recovery_startup_poll(const uint16_t rows[5]);

/* Native event-source recovery, shared by the merged and legacy behaviors. */
struct zmk_behavior_binding_event;
int gmk_recovery_hold_press(enum gmk_recovery_action action, struct zmk_behavior_binding_event event);
int gmk_recovery_release_event(struct zmk_behavior_binding_event event);
