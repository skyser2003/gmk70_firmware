/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>

/* Called once during early startup, after SoC clocks are established. Handles
 * both stopped and inherited running IWDG. False means no verified long-period
 * configuration; gate mutations and do not claim watchdog health on failure.
 * Staging qualification returns false and makes no hardware accesses. */
bool gmk_zmk_watchdog_init(void);
/* Dedicated live housekeeping context feeds only after successful init.
 * Staging qualification never refreshes the short rollback watchdog. */
void gmk_zmk_watchdog_feed(void);
