/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

struct gmk_flip_core {
    /* Bits 8..9 retain off/Windows/Mac; low byte retains implicit modifiers. */
    uint16_t held[24];
    bool     enabled;
};
struct gmk_flip_output {
    uint32_t key, previous;
    int8_t   brightness;
    bool     emit, release_previous;
};
struct gmk_flip_output gmk_flip_core_event(struct gmk_flip_core *core, uint32_t encoded, bool pressed, bool mac);
