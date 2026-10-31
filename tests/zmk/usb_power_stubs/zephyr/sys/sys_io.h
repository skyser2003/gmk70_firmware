/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
void     sys_set_bits(uintptr_t address, uint32_t bits);
uint32_t sys_read32(uintptr_t address);
void     sys_write32(uint32_t value, uintptr_t address);
void     sys_clear_bits(uintptr_t address, uint32_t bits);
