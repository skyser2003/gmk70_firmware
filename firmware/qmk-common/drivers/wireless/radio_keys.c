/* Copyright 2024 Su (@isuua)
 * Copyright 2026 GMK70 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "radio_keys.h"
#include <stdbool.h>
#include <string.h>

static bool held(const uint8_t *bits, size_t bytes, uint8_t key) {
    return key && key / 8 < bytes && (bits[key / 8] & (1U << (key % 8)));
}

static uint8_t slot_for(struct gmk70_radio_keys *state, uint8_t key, bool high) {
    uint8_t empty = 6, low = 6;
    for (uint8_t slot = 0; slot < 6; ++slot) {
        if (state->keys[slot] == key) return slot;
        if (!state->keys[slot] && empty == 6) empty = slot;
        if (state->keys[slot] && state->keys[slot] < 112 && low == 6) low = slot;
    }
    return empty < 6 ? empty : high ? low : 6;
}

void gmk70_radio_keys_encode(struct gmk70_radio_keys *state, uint8_t modifiers, const uint8_t *bits, size_t bytes, uint8_t keyboard[8], uint8_t overflow[14]) {
    memset(keyboard, 0, 8);
    memset(overflow, 0, 14);
    if (!bits) {
        memset(state, 0, sizeof(*state));
        return;
    }
    keyboard[0] = modifiers;
    for (uint8_t slot = 0; slot < 6; ++slot) {
        if (!held(bits, bytes, state->keys[slot])) state->keys[slot] = 0;
    }
    /* Higher usages have no A2 bit. Move a lower usage to A2 when its slot is
     * needed, retaining every other held slot and never truncating a usage. */
    for (uint16_t key = 112; key < 256; ++key) {
        if (!held(bits, bytes, (uint8_t)key)) continue;
        uint8_t slot = slot_for(state, (uint8_t)key, true);
        if (slot < 6) state->keys[slot] = (uint8_t)key;
    }
    for (uint8_t key = 1; key < 112; ++key) {
        if (!held(bits, bytes, key)) continue;
        uint8_t slot = slot_for(state, key, false);
        if (slot < 6)
            state->keys[slot] = key;
        else
            overflow[key / 8] |= (uint8_t)(1U << (key % 8));
    }
    memcpy(keyboard + 2, state->keys, sizeof(state->keys));
}
