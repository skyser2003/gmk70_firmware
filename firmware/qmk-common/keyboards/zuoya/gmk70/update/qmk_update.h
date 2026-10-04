// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef GMK70_SPLIT_UPDATE
void gmk_update_init(void);
void gmk_update_task(void);
bool gmk_update_active(void);
bool gmk_update_usb(uint8_t *data, uint8_t length);
#endif
