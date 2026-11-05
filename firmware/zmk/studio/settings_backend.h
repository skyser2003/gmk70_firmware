/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "receiver.h"
#include <stddef.h>

bool gmk_zmk_settings_ready(void);
/* Studio binding writes require every active native behavior ID/name pair to
 * be durably present. Ordinary runtime preferences use mount readiness only. */
bool gmk_zmk_settings_ids_ready(void);
/* Runtime writes recheck authorization at dispatch. Wireless admission is
 * restricted to the board preference allowlist and safe idle power state;
 * startup/Studio writes retain the wired gate and original USB behavior. */
int gmk_zmk_settings_save_runtime(const char *name, const void *value, size_t size);
/* Deliberate LEFT wired/configured-USB recovery. One queued empty-bank commit,
 * protected by the updater storage lease; caller reboots after success. */
int  gmk_zmk_settings_factory_reset(void);
void gmk_zmk_settings_poll(const gmk_zmk_update_env_t *env);
bool gmk_zmk_settings_flash_page(uint32_t address, const uint8_t data[256]);
