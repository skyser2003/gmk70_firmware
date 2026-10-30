/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "receiver.h"
#include <stddef.h>

bool gmk_zmk_settings_ready(void);
/* Board runtime writes recheck USB at dispatch; startup settings stay usable
 * before enumeration. Both paths share the same queue, bank and storage lock. */
int  gmk_zmk_settings_save_runtime(const char *name, const void *value, size_t size);
void gmk_zmk_settings_poll(const gmk_zmk_update_env_t *env);
bool gmk_zmk_settings_flash_page(uint32_t address, const uint8_t data[256]);
