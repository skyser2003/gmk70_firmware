/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* Physical half only: packed RGB order, at most 41 pixels, thread context. */
int gmk_pixels_init(void);
int gmk_pixels_write(const uint8_t *rgb, size_t count);
