/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
uint32_t gmk_wb32_irq_lock(void);
void     gmk_wb32_irq_restore(uint32_t state);
void     gmk_wb32_flash_preop(void);
void     gmk_wb32_flash_ram_operation(void);
