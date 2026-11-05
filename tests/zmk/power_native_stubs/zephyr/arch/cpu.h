/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
extern uint32_t    test_primask, test_basepri;
static inline void __disable_irq(void) {
    test_primask = 1;
}
static inline void __set_BASEPRI(uint32_t value) {
    test_basepri = value;
}
static inline void __set_PRIMASK(uint32_t value) {
    test_primask = value;
}
static inline uint32_t __get_BASEPRI(void) {
    return test_basepri;
}
static inline uint32_t __get_PRIMASK(void) {
    return test_primask;
}
void               test_power_wfi(void);
static inline void __WFI(void) {
    test_power_wfi();
}
static inline void __DSB(void) {}
static inline void __ISB(void) {}
