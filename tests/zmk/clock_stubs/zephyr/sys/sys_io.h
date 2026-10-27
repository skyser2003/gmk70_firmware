/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
uint32_t           sys_read32(uintptr_t address);
void               sys_write32(uint32_t value, uintptr_t address);
static inline void sys_set_bits(uintptr_t address, uint32_t mask) {
    sys_write32(sys_read32(address) | mask, address);
}
static inline void sys_clear_bits(uintptr_t address, uint32_t mask) {
    sys_write32(sys_read32(address) & ~mask, address);
}
