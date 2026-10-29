/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>

/* Experimental update activation only. False leaves the legacy reset path
 * available; accepted handoff never returns to the application being replaced. */
bool gmk_zmk_resident_handoff(void);
