/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stddef.h>
#include <stdint.h>

struct gmk70_radio_keys {
    uint8_t keys[6];
};

/* Stock A2 has bits for usages 0..111. The six A1 slots also carry higher
 * byte-sized usages; at most six of those can be represented simultaneously. */
void gmk70_radio_keys_encode(struct gmk70_radio_keys *state, uint8_t modifiers, const uint8_t *bits, size_t bytes, uint8_t keyboard[8], uint8_t overflow[14]);
