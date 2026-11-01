/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

struct gmk_flip_core {
    /* High byte: off/Windows/Mac/canceled; low byte: source modifiers. */
    uint16_t held[24];
    bool     enabled;
};
struct gmk_flip_output {
    uint32_t key, previous;
    int8_t   brightness;
    uint8_t  remaining_modifiers;
    uint8_t  source;
    bool     emit, release_previous, custom, previous_custom;
};
struct gmk_flip_output gmk_flip_core_event(struct gmk_flip_core *core, uint32_t encoded, bool pressed, bool mac);
uint8_t                gmk_flip_core_modifiers(const struct gmk_flip_core *core);
void                   gmk_flip_core_clear(struct gmk_flip_core *core, uint16_t page);
