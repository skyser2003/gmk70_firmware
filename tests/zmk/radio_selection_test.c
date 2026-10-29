/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../firmware/zmk/radio/selection.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>

static void switch_debounces_initial_and_changed_modes(void) {
    struct gmk_radio_selector selector = {0};
    uint8_t                   output   = 77;
    assert(!gmk_radio_selector_switch(&selector, 3, 100, 4, &output) && output == 77);
    assert(!gmk_radio_selector_switch(&selector, 3, 149, 4, &output) && output == 77);
    assert(gmk_radio_selector_switch(&selector, 3, 150, 4, &output) && output == 0);
    assert(!gmk_radio_selector_switch(&selector, 3, 151, 4, &output));
    assert(!gmk_radio_selector_switch(&selector, 2, 200, 4, &output));
    assert(!gmk_radio_selector_switch(&selector, 1, 249, 4, &output));
    assert(!gmk_radio_selector_switch(&selector, 1, 298, 4, &output));
    assert(gmk_radio_selector_switch(&selector, 1, 299, 4, &output) && output == 6);
    assert(!gmk_radio_selector_switch(&selector, 0, 300, 4, &output));
    assert(gmk_radio_selector_switch(&selector, 0, 350, 4, &output) && output == 255);
    assert(!gmk_radio_selector_switch(&selector, 0, 400, 4, &output));
    assert(!gmk_radio_selector_switch(&selector, 3, 401, 4, &output));
    assert(gmk_radio_selector_switch(&selector, 3, 451, 4, &output) && output == 0);
}
static void bluetooth_profiles_and_fallback_are_bounded(void) {
    const uint8_t profiles[] = {0, 1, 2, 3, 4, 5, 6, 255};
    const uint8_t expected[] = {1, 1, 2, 3, 4, 5, 1, 1};
    for (unsigned i = 0; i < sizeof(profiles); ++i) {
        struct gmk_radio_selector selector = {0};
        uint8_t                   output   = 77;
        assert(!gmk_radio_selector_switch(&selector, 2, 0, profiles[i], &output));
        assert(gmk_radio_selector_switch(&selector, 2, 50, profiles[i], &output) && output == expected[i]);
        assert(!gmk_radio_selector_switch(&selector, 2, 100, 5, &output));
    }
}
static void pair_fires_once_at_three_seconds(void) {
    struct gmk_radio_selector selector = {0};
    uint8_t                   output = 77, channel = 0;
    uint32_t                  position = 0;
    assert(!gmk_radio_selector_holding(&selector, &position, &channel));
    assert(gmk_radio_selector_press(&selector, 5, 71, 100) == 0);
    assert(gmk_radio_selector_holding(&selector, &position, &channel) && position == 71 && channel == 5);
    assert(!gmk_radio_selector_pair(&selector, 3099, &output) && output == 77);
    assert(gmk_radio_selector_pair(&selector, 3100, &output) && output == 5);
    assert(!gmk_radio_selector_pair(&selector, 3101, &output));
    assert(!gmk_radio_selector_holding(&selector, &position, &channel));
}
static void releases_replacement_usb_and_cancel_stop_old_holds(void) {
    struct gmk_radio_selector selector = {0};
    uint8_t                   output   = 77;
    assert(gmk_radio_selector_press(&selector, 1, 2, 0) == 0);
    gmk_radio_selector_release(&selector, 3);
    assert(gmk_radio_selector_pair(&selector, 3000, &output) && output == 1);
    assert(gmk_radio_selector_press(&selector, 2, 2, 4000) == 0);
    gmk_radio_selector_release(&selector, 2);
    assert(!gmk_radio_selector_pair(&selector, 7000, &output));
    assert(gmk_radio_selector_press(&selector, 3, 2, 8000) == 0);
    assert(gmk_radio_selector_press(&selector, 6, 4, 9000) == 0);
    gmk_radio_selector_release(&selector, 2);
    assert(!gmk_radio_selector_pair(&selector, 11999, &output));
    assert(gmk_radio_selector_pair(&selector, 12000, &output) && output == 6);
    assert(gmk_radio_selector_press(&selector, 4, 0, 13000) == 0);
    assert(gmk_radio_selector_press(&selector, 0, 1, 14000) == 0);
    assert(!gmk_radio_selector_pair(&selector, 17000, &output));
    assert(gmk_radio_selector_press(&selector, 4, 0, 18000) == 0);
    gmk_radio_selector_cancel(&selector);
    assert(!gmk_radio_selector_pair(&selector, 21000, &output));
}
static void switch_changes_cancel_before_debounce_and_wrap_is_safe(void) {
    struct gmk_radio_selector selector = {0};
    uint8_t                   output   = 77;
    assert(!gmk_radio_selector_switch(&selector, 3, UINT32_MAX - 20, 1, &output));
    assert(!gmk_radio_selector_switch(&selector, 3, 28, 1, &output));
    assert(gmk_radio_selector_switch(&selector, 3, 29, 1, &output) && output == 0);
    assert(gmk_radio_selector_press(&selector, 3, 0, UINT32_MAX - 1000) == 0);
    assert(!gmk_radio_selector_pair(&selector, 1998, &output));
    assert(gmk_radio_selector_pair(&selector, 1999, &output) && output == 3);
    assert(gmk_radio_selector_press(&selector, 3, 0, 3000) == 0);
    assert(!gmk_radio_selector_switch(&selector, 2, 5999, 1, &output));
    assert(!gmk_radio_selector_pair(&selector, 6000, &output));
}
static void invalid_inputs_never_truncate_channels_or_positions(void) {
    struct gmk_radio_selector selector = {0};
    uint8_t                   output   = 77;
    assert(gmk_radio_selector_press(&selector, 7, 0, 0) == -EINVAL);
    assert(gmk_radio_selector_press(&selector, 255, 0, 0) == -EINVAL);
    assert(gmk_radio_selector_press(&selector, 1, 72, 0) == -EINVAL);
    assert(gmk_radio_selector_press(&selector, 1, UINT32_MAX, 0) == -EINVAL);
    assert(!gmk_radio_selector_pair(&selector, 3000, &output) && output == 77);
    assert(!gmk_radio_selector_switch(&selector, 4, 0, 1, &output));
    assert(!gmk_radio_selector_switch(&selector, 255, 100, 1, &output) && output == 77);
    assert(gmk_radio_selector_press(&selector, 1, 0, 1000) == 0);
    assert(!gmk_radio_selector_switch(&selector, 4, 3999, 1, &output));
    assert(!gmk_radio_selector_pair(&selector, 4000, &output));
    assert(gmk_radio_selector_press(&selector, 2, 71, 5000) == 0);
    assert(gmk_radio_selector_press(&selector, 255, 0, 5001) == -EINVAL);
    gmk_radio_selector_release(&selector, UINT32_MAX);
    assert(gmk_radio_selector_pair(&selector, 8000, &output) && output == 2);
}
int main(void) {
    assert(sizeof(struct gmk_radio_selector) < 24);
    switch_debounces_initial_and_changed_modes();
    bluetooth_profiles_and_fallback_are_bounded();
    pair_fires_once_at_three_seconds();
    releases_replacement_usb_and_cancel_stop_old_holds();
    switch_changes_cancel_before_debounce_and_wrap_is_safe();
    invalid_inputs_never_truncate_channels_or_positions();
    puts("Radio selector: debounce, stock switch modes, bounded profiles, hold cancellation and timer wrap passed");
    return 0;
}
