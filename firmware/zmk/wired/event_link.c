/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "event_link.h"
#include "wired_protocol.h"
#include <string.h>

void gmk_event_enable(struct gmk_event_link *link, const uint16_t rows[5], bool quiet) {
    *link = (struct gmk_event_link){.enabled = true, .sequence = 1, .quiet = quiet, .tag = 1};
    memcpy(link->rows, rows, sizeof(link->rows));
    memcpy(link->queued[0], rows, sizeof(link->rows));
    link->count = 1; /* The first snapshot also establishes the new session. */
}
void gmk_event_sample(struct gmk_event_link *link, const uint16_t rows[5], bool quiet) {
    link->quiet = quiet;
    if (!link->enabled || !memcmp(rows, link->rows, sizeof(link->rows))) return;
    memcpy(link->rows, rows, sizeof(link->rows));
    if (link->count == GMK_EVENT_QUEUE) {
        /* Retain the in-flight head until acknowledged. Coalesce only the
         * newest queued snapshot on overflow; never leave a stale held key. */
        memcpy(link->queued[(link->head + link->count - 1U) % GMK_EVENT_QUEUE], rows, sizeof(link->rows));
        link->overflow = true;
        return;
    }
    memcpy(link->queued[(link->head + link->count) % GMK_EVENT_QUEUE], rows, sizeof(link->rows));
    ++link->count;
    link->notified = false;
}
void gmk_event_reply(struct gmk_event_link *link, uint16_t acknowledged, uint8_t reply[GMK_EVENT_FRAME]) {
    if (link->count && acknowledged == link->sequence) {
        link->head = (uint8_t)((link->head + 1U) % GMK_EVENT_QUEUE);
        --link->count;
        ++link->sequence;
        if (!link->sequence) ++link->sequence; /* Zero means no acknowledgement. */
        if (!link->count) link->overflow = false;
    }
    reply[0]             = (uint8_t)link->sequence;
    reply[1]             = (uint8_t)(link->sequence >> 8);
    reply[2]             = (link->count ? GMK_EVENT_DATA : 0) | (link->quiet ? GMK_EVENT_QUIET : 0) | (link->overflow ? GMK_EVENT_OVERFLOW : 0);
    const uint16_t *rows = link->count ? link->queued[link->head] : link->rows;
    for (unsigned row = 0; row < 5; ++row) {
        reply[3 + 2 * row] = (uint8_t)rows[row];
        reply[4 + 2 * row] = (uint8_t)(rows[row] >> 8);
    }
    reply[13] = 0xe5;
    reply[14] = link->tag;
    reply[15] = gmk_wire_crc8(reply, 15);
}
bool gmk_event_decode(const uint8_t reply[GMK_EVENT_FRAME], uint16_t *sequence, uint8_t *flags, uint16_t rows[5]) {
    if (reply[13] != 0xe5 || !reply[14] || reply[15] != gmk_wire_crc8(reply, 15) || (reply[2] & ~7U)) return false;
    uint16_t value = (uint16_t)(reply[0] | ((uint16_t)reply[1] << 8));
    if (!value) return false;
    for (unsigned row = 0; row < 5; ++row) {
        rows[row] = (uint16_t)(reply[3 + 2 * row] | ((uint16_t)reply[4 + 2 * row] << 8));
        if (rows[row] & ~0x1ffU) return false;
    }
    *sequence = value;
    *flags    = reply[2];
    return true;
}
bool gmk_event_notice_due(const struct gmk_event_link *link, uint32_t now) {
    return link->enabled && link->count && (!link->notified || (uint32_t)(now - link->notice_at) >= 20U);
}
void gmk_event_notified(struct gmk_event_link *link, uint32_t now) {
    link->notified  = true;
    link->notice_at = now;
}
bool gmk_event_control(struct gmk_event_link *link, const uint16_t rows[5], bool quiet, const uint8_t request[32], uint8_t response[32]) {
    if (memcmp(request, "EVT\1", 4)) return false;
    memset(response, 0, 32);
    if (request[4] > 1 || request[31] != gmk_wire_crc8(request, 31)) return true;
    uint32_t token = (uint32_t)request[8] | ((uint32_t)request[9] << 8) | ((uint32_t)request[10] << 16) | ((uint32_t)request[11] << 24);
    if (request[4]) {
        if (!token) return true;
        /* A fresh/rebooted master must not discard a tap queued while its
         * native settings loaded. Rekey the retained queue without ACKing it.
         * The board disarms notices until a poll validates the new tag. */
        if (!link->enabled) gmk_event_enable(link, rows, quiet);
        link->tag   = (uint8_t)(gmk_wire_crc8(request + 8, 4) | 1U);
        link->token = token;
    }
    memcpy(response, request, 32);
    response[5]  = 1;
    response[6]  = link->enabled && token == link->token;
    response[31] = gmk_wire_crc8(response, 31);
    return true;
}
