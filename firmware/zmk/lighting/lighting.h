/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "wired_protocol.h"
#ifdef CONFIG_GMK70_WB32_RGB
void gmk_lighting_receive(const uint8_t packet[16]);
void gmk_lighting_wire_poll(const struct gmk_wire_io *);
void gmk_lighting_poll(void);
#else
static inline void gmk_lighting_receive(const uint8_t packet[16]) {
    (void)packet;
}
static inline void gmk_lighting_wire_poll(const struct gmk_wire_io *io) {
    (void)io;
}
static inline void gmk_lighting_poll(void) {}
#endif
