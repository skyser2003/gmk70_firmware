// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include "stock_power.h"
int main(void) {
    assert(stock_power_prepare_allowed(false));
    for (unsigned i = 0; i < 8; ++i)
        stock_power_key(true);
    for (unsigned i = 0; i < 4; ++i) {
        assert(!stock_power_prepare_allowed(false));
        assert(stock_power_prepare_allowed(true));
        stock_power_key(false);
    }
    assert(stock_power_prepare_allowed(false));
    for (unsigned i = 0; i < 8; ++i)
        stock_power_key(false);
    assert(stock_power_prepare_allowed(false));
    stock_power_key(true);
    assert(!stock_power_prepare_allowed(false));
    stock_power_reset_keys();
    assert(stock_power_prepare_allowed(false));
    const uint32_t expected[] = {0, 60000, 180000, 300000, 600000, 1200000, 1800000, 300000};
    for (unsigned i = 0; i < 256; ++i)
        assert(stock_power_timeout(i) == expected[i & 7]);
    puts("Stock held-key saturation, critical-battery bypass and all timeout selectors passed");
}
