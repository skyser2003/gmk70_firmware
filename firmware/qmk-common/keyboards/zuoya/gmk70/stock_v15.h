// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

uint8_t gmk70_stock_sleep_get(void);
void    gmk70_stock_sleep_set(uint8_t selector);
uint8_t gmk70_stock_nkro_get(void);
void    gmk70_stock_nkro_set(uint8_t enabled);
uint8_t gmk70_stock_brightness_get(void);
void    gmk70_stock_brightness_set(uint8_t value);
void    gmk70_stock_activity(void);
void    gmk70_stock_request_reset(void);
void    gmk70_stock_request_bootloader(void);
