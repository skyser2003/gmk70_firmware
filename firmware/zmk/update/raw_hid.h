/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "receiver.h"
int gmk_zmk_raw_hid_init(void);
/* Runs only in the updater worker. Callbacks merely enqueue 32-byte reports. */
void gmk_zmk_raw_hid_poll(const gmk_zmk_update_env_t *env);
