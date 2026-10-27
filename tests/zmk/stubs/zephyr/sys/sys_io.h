#pragma once
#include <stdint.h>
uint8_t sys_read8(uintptr_t addr);
void    sys_write8(uint8_t value, uintptr_t addr);
