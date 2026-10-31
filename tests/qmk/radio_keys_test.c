/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "radio_keys.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void stable_slots_keep_holes_and_high_usage_boundaries(void) {
    struct gmk70_radio_keys state    = {0};
    uint8_t                 bits[33] = {0xf1, 0x0f};
    uint8_t                 keyboard[8], overflow[14];
    gmk70_radio_keys_encode(&state, 0xa5, bits, sizeof(bits), keyboard, overflow);
    const uint8_t first[8]        = {0xa5, 0, 4, 5, 6, 7, 8, 9};
    const uint8_t first_extra[14] = {0, 0x0c};
    assert(memcmp(keyboard, first, sizeof(first)) == 0);
    assert(memcmp(overflow, first_extra, sizeof(first_extra)) == 0);

    bits[0]  = 0x20;
    bits[1]  = 2;
    bits[13] = 0x80; /* Last A2 usage: 111. */
    bits[14] = 1;    /* First usage with no A2 bit: 112. */
    bits[31] = 0x80; /* Last byte-sized A1 usage: 255. */
    bits[32] = 1;    /* Cannot fit in an A1 slot and must never wrap to zero. */
    gmk70_radio_keys_encode(&state, 3, bits, sizeof(bits), keyboard, overflow);
    const uint8_t holes[8] = {3, 0, 112, 5, 255, 111, 0, 9};
    const uint8_t zero[14] = {0};
    assert(memcmp(keyboard, holes, sizeof(holes)) == 0);
    assert(memcmp(overflow, zero, sizeof(zero)) == 0);

    /* A shorter bitmap releases old high slots without reading past its end. */
    gmk70_radio_keys_encode(&state, 0, bits, 1, keyboard, overflow);
    const uint8_t shortened[8] = {0, 0, 0, 5};
    assert(memcmp(keyboard, shortened, sizeof(shortened)) == 0);
    assert(memcmp(overflow, zero, sizeof(zero)) == 0);
    memset(keyboard, 0xff, sizeof(keyboard));
    memset(overflow, 0xff, sizeof(overflow));
    gmk70_radio_keys_encode(&state, 0xff, NULL, 0, keyboard, overflow);
    const uint8_t released[8] = {0};
    assert(memcmp(keyboard, released, sizeof(released)) == 0);
    assert(memcmp(overflow, zero, sizeof(zero)) == 0);
}

static void six_high_usages_leave_all_lower_usages_in_the_bitmap(void) {
    struct gmk70_radio_keys state    = {0};
    uint8_t                 bits[32] = {0};
    uint8_t                 keyboard[8], overflow[14];
    memset(bits, 0xff, 14);
    bits[14] = 0x7f;
    gmk70_radio_keys_encode(&state, 0, bits, sizeof(bits), keyboard, overflow);
    const uint8_t slots[8]  = {0, 0, 112, 113, 114, 115, 116, 117};
    const uint8_t extra[14] = {0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    assert(memcmp(keyboard, slots, sizeof(slots)) == 0);
    assert(memcmp(overflow, extra, sizeof(extra)) == 0);
    /* Releasing a high usage promotes the previously unreportable seventh. */
    bits[14] &= (uint8_t)~2U;
    gmk70_radio_keys_encode(&state, 0, bits, sizeof(bits), keyboard, overflow);
    const uint8_t promoted[8] = {0, 0, 112, 118, 114, 115, 116, 117};
    assert(memcmp(keyboard, promoted, sizeof(promoted)) == 0);
    assert(memcmp(overflow, extra, sizeof(extra)) == 0);
}

int main(void) {
    stable_slots_keep_holes_and_high_usage_boundaries();
    six_high_usages_leave_all_lower_usages_in_the_bitmap();
    puts("Radio keys: stable slots, high-usage priority, A2 bounds and complete release passed");
    return 0;
}
