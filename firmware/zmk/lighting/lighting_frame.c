/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lighting_frame.h"
#include "wired_protocol.h"
#include <string.h>

#define MAGIC 0xd7U
#define COMPLETE ((1U << GMK_LIGHT_CHUNKS) - 1U)

bool gmk_light_queue(struct gmk_light_frame *f, const uint8_t rgb[3 * GMK_LIGHT_RIGHT]) {
    if (f->busy) return false;
    memcpy(f->rgb, rgb, sizeof(f->rgb));
    ++f->sequence;
    f->chunk   = 0;
    f->pending = true;
    f->cached  = true;
    return true;
}
bool gmk_light_refresh(struct gmk_light_frame *f) {
    if (!f->cached || f->pending || f->busy) return false;
    ++f->sequence;
    f->chunk   = 0;
    f->pending = true;
    return true;
}
bool gmk_light_next(const struct gmk_light_frame *f, uint8_t packet[16]) {
    if (!f->pending) return false;
    memset(packet, 0, 16);
    packet[0]       = MAGIC;
    packet[1]       = f->sequence;
    packet[2]       = f->chunk;
    unsigned offset = 12U * f->chunk;
    unsigned n      = sizeof(f->rgb) - offset;
    if (n > 12) n = 12;
    memcpy(packet + 3, f->rgb + offset, n);
    packet[15] = gmk_wire_crc8(packet, 15);
    return true;
}
void gmk_light_sent(struct gmk_light_frame *f, const uint8_t packet[16], bool success) {
    if (!f->pending || f->sequence != packet[1] || f->chunk != packet[2]) return;
    if (!success || ++f->chunk == GMK_LIGHT_CHUNKS) f->pending = false;
}
bool gmk_light_accept(struct gmk_light_frame *f, const uint8_t packet[16]) {
    unsigned chunk = packet[2];
    if (f->busy || packet[0] != MAGIC || chunk >= GMK_LIGHT_CHUNKS || packet[15] != gmk_wire_crc8(packet, 15)) return false;
    if (chunk == 0) {
        f->sequence  = packet[1];
        f->mask      = 0;
        f->receiving = true;
        f->ready     = false;
    }
    if (!f->receiving || f->sequence != packet[1]) return false;
    unsigned offset = 12 * chunk;
    unsigned n      = sizeof(f->rgb) - offset;
    if (n > 12) n = 12;
    memcpy(f->rgb + offset, packet + 3, n);
    f->mask |= (uint16_t)(1U << chunk);
    if (f->mask == COMPLETE) {
        f->ready     = true;
        f->receiving = false;
    }
    return true;
}
bool gmk_light_claim(struct gmk_light_frame *f) {
    if (!f->ready || f->busy) return false;
    f->busy = true;
    return true;
}
void gmk_light_release(struct gmk_light_frame *f) {
    f->busy = f->ready = f->receiving = false;
    f->mask                           = 0;
}
void gmk_light_reset(struct gmk_light_frame *f) {
    if (!f->busy) memset(f, 0, sizeof(*f));
}
