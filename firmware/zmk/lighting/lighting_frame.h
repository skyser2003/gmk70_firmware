/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define GMK_LIGHT_LEFT 36U
#define GMK_LIGHT_RIGHT 41U
#define GMK_LIGHT_PIXELS (GMK_LIGHT_LEFT + GMK_LIGHT_RIGHT)
#define GMK_LIGHT_CHUNKS 11U

/* One bounded buffer per physical half. Caller serializes all accesses and
 * retains a claim through hardware encoding, outside the wire/IRQ lock. */
struct gmk_light_frame {
    uint8_t  rgb[3 * GMK_LIGHT_RIGHT];
    uint16_t mask;
    uint8_t  sequence, chunk;
    bool     pending, receiving, ready, busy, cached;
};
bool gmk_light_queue(struct gmk_light_frame *, const uint8_t rgb[3 * GMK_LIGHT_RIGHT]);
bool gmk_light_next(const struct gmk_light_frame *, uint8_t packet[16]);
bool gmk_light_refresh(struct gmk_light_frame *);
void gmk_light_sent(struct gmk_light_frame *, const uint8_t packet[16], bool success);
bool gmk_light_accept(struct gmk_light_frame *, const uint8_t packet[16]);
bool gmk_light_claim(struct gmk_light_frame *);
void gmk_light_release(struct gmk_light_frame *);
void gmk_light_reset(struct gmk_light_frame *);
