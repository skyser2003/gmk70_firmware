/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>

/* Called by the existing update worker; persistence itself runs on system work. */
void gmk_rgb_settings_poll(bool writes_allowed);
/* Dirty also covers native's pending idle snapshot. A one-minute sleep choice
 * may wait for the native debounce (30s IDLE plus 60s save) before safe entry. */
bool gmk_rgb_settings_dirty(void);
/* Reload the pinned native RGB defaults and replace any queued old snapshot. */
int gmk_rgb_settings_reset(void);
