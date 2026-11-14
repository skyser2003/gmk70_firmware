/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stddef.h>
#include <stdint.h>
uint32_t crc32_ieee(const uint8_t *data, size_t size);
uint32_t crc32_ieee_update(uint32_t crc, const uint8_t *data, size_t size);
