/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
uint32_t           sys_read32(uintptr_t address);
void               sys_write32(uint32_t value, uintptr_t address);
static inline void sys_set_bits(uintptr_t address, uint32_t bits) {
    sys_write32(sys_read32(address) | bits, address);
}
