/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Bounded 64/32 division using the M3's native 32-bit divider, then 32
 * restoring steps. Track the remainder's 33rd bit explicitly; subtraction
 * wraps to its exact final value below the nonzero divisor. */
static uint64_t gmk_sleep_divide(uint64_t numerator, uint32_t denominator) {
    uint32_t high      = numerator >> 32;
    uint32_t upper     = high / denominator;
    uint32_t remainder = high % denominator;
    uint32_t lower = (uint32_t)numerator, quotient = 0;
    for (unsigned bit = 0; bit < 32; ++bit) {
        bool carry = (remainder >> 31) != 0;
        remainder  = remainder << 1 | lower >> 31;
        lower <<= 1;
        quotient <<= 1;
        if (carry || remainder >= denominator) {
            remainder -= denominator;
            quotient |= 1;
        }
    }
    return (uint64_t)upper << 32 | quotient;
}
