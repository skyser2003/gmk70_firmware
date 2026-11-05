/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>

bool gmk_nkro_enabled(void);
int  gmk_nkro_set(bool enabled);
int  gmk_nkro_toggle(void);
void gmk_nkro_poll(bool writes_allowed);
bool gmk_nkro_dirty(void);
