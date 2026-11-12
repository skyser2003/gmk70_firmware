/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool     gmk_controls_gui_locked(void);
int      gmk_controls_gui_lock_set(bool locked);
int      gmk_controls_gui_lock_toggle(void);
bool     gmk_controls_mac_mode(void);
int      gmk_controls_mac_set(bool mac);
int      gmk_controls_mac_toggle(void);
uint8_t  gmk_controls_bt_slot(void);
int      gmk_controls_bt_slot_set(uint8_t slot);
uint8_t  gmk_controls_sleep_selector(void);
int      gmk_controls_sleep_selector_set(uint8_t selector);
uint32_t gmk_controls_sleep_timeout_ms(void);
bool     gmk_controls_usb_sleep(void);
int      gmk_controls_usb_sleep_set(bool enabled);

/* Queues one debounced system-workqueue save. The storage backend rechecks
 * authorization when it dispatches; callers supply the current runtime gate. */
void gmk_controls_poll(bool writes_allowed);
bool gmk_controls_dirty(void);
/* RAM defaults after the settings bank has been cleared by the physical key. */
void gmk_controls_reset(void);

/* Apply after all native/board modifier contributions in the existing report
 * getter. This never changes native modifier counts or key release ownership. */
uint8_t gmk_controls_project_modifiers(uint8_t modifiers);

/* The external module uses stock channel/connection enums from radio/core.h.
 * Only accepted notifications belonging to the active host may supply LEDs. */
void gmk_controls_radio_state(uint8_t selected, uint8_t connection);
void gmk_controls_radio_leds(uint8_t indicators);

/* The lighting owner serializes this composition with normal/battery rendering.
 * Pixels use stock physical order; count can be the full 77 or a LEFT prefix.
 * Apply before held battery overlays. The caller admits physical RGB power and
 * native activity policy through enabled. A RIGHT never renders local controls.
 * The token changes on state/blink transitions so RGB-off frames can refresh. */
void     gmk_controls_overlay(uint8_t *rgb, size_t count, uint32_t now, bool left, bool enabled);
uint32_t gmk_controls_visual_token(uint32_t now);
uint32_t gmk_controls_visual_delay(uint32_t now);
