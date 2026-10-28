/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#define WB32_RCC_BASE 0x40010c00U
/* Test-only register wrapper observes the actual C driver's direct MMIO writes.
 * Build that unchanged source as C++ solely for assignment interception. */
struct power_register {
    unsigned index;
             operator uint32_t() const;
    void     operator=(uint32_t value);
};
struct wb32_gpio {
    power_register moder, otyper, ospeedr, pupdr, idr, odr, bsrr, lckr;
    power_register afrl, afrh, smit, current, cfgmsk;
};
