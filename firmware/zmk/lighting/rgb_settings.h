/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>

/* Called by the existing update worker; persistence itself runs on system work. */
void gmk_rgb_settings_poll(bool writes_allowed);
