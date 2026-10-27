/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
uint32_t sys_read32(uintptr_t address);
void     sys_write32(uint32_t value, uintptr_t address);
