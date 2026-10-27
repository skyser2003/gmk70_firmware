#pragma once
void test_dsb(void);
void test_reset(void);
#define __DSB() test_dsb()
#define NVIC_SystemReset() test_reset()
