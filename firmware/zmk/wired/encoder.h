/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define GMK_ENCODER_QUEUE 16
struct gmk_encoder {
    int8_t  queue[GMK_ENCODER_QUEUE], pulses;
    uint8_t resolution, head, count;
};
/* Caller serializes native EC11 ticks and dequeue. -1 is CCW; +1 is CW. */
void gmk_encoder_init(struct gmk_encoder *, uint8_t resolution);
void gmk_encoder_sample(struct gmk_encoder *, int32_t ticks);
int  gmk_encoder_take(struct gmk_encoder *);
void gmk_encoder_clear(struct gmk_encoder *);
/* Deferred device initialization: caller must have detected LEFT first. */
bool    gmk_native_encoder_init(void);
int32_t gmk_native_encoder_sample(void);
