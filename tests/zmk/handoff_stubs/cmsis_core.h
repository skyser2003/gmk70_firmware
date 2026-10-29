#pragma once
#include <stdint.h>
uint32_t __get_CONTROL(void);
uint32_t __get_IPSR(void);
void __disable_irq(void);
