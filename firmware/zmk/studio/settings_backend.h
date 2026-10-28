/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "receiver.h"

bool gmk_zmk_settings_ready(void);
void gmk_zmk_settings_poll(const gmk_zmk_update_env_t *env);
bool gmk_zmk_settings_flash_page(uint32_t address, const uint8_t data[256]);
