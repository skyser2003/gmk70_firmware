/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../runtime/service.h"
#include <stddef.h>

struct settings_handler_static;
/* Reload a board-owned native state record in RAM, without persisting it. */
int gmk_zmk_settings_restore_state(const struct settings_handler_static *handler, const void *value, size_t size);

bool gmk_zmk_settings_ready(void);
/* Studio binding writes require every active native behavior ID/name pair to
 * be durably present. Ordinary runtime preferences use mount readiness only. */
bool gmk_zmk_settings_ids_ready(void);
/* Runtime writes recheck authorization at dispatch. Wireless admission is
 * restricted to the board preference allowlist and safe idle power state;
 * startup/Studio writes retain the wired gate and original USB behavior. */
int gmk_zmk_settings_save_runtime(const char *name, const void *value, size_t size);
/* Deliberate LEFT wired/configured-USB recovery. One queued empty-bank commit,
 * protected by the shared storage lease; caller reboots after success. */
int gmk_zmk_settings_factory_reset(void);
/* Deliberate physical key only. Restore RAM after the atomic empty-bank commit;
 * normal Studio/generic writes keep their original authorization. */
int  gmk_zmk_settings_reset_local(int (*restore)(void));
void gmk_zmk_settings_poll(const gmk_zmk_service_env_t *env);
bool gmk_zmk_settings_flash_page(uint32_t address, const uint8_t data[256]);
