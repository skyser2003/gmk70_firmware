/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
int     gmk_flip_toggle(void);
void    gmk_flip_poll(bool writes_allowed);
bool    gmk_flip_dirty(void);
uint8_t gmk_flip_modifiers(void);
void    gmk_flip_clear(uint16_t page);
/* Enclose the native clear and its board hooks; recursive for same-thread clears. */
void gmk_flip_clear_begin(void);
void gmk_flip_clear_end(void);
