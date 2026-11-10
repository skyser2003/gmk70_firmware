/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define GMK_EVENT_QUEUE 8U
#define GMK_EVENT_FRAME 16U
#define GMK_EVENT_NOTICE 0xfcU
#define GMK_EVENT_HEARTBEAT_MS 500U
#define GMK_EVENT_LEASE_MS 1500U
enum { GMK_EVENT_DATA = 1, GMK_EVENT_QUIET = 2, GMK_EVENT_OVERFLOW = 4 };
struct gmk_event_link {
    uint16_t rows[5], queued[GMK_EVENT_QUEUE][5];
    uint16_t sequence;
    uint8_t  head, count, tag;
    bool     enabled, quiet, overflow, notified;
    uint32_t notice_at;
    uint32_t token;
};
void gmk_event_enable(struct gmk_event_link *link, const uint16_t rows[5], bool quiet);
void gmk_event_sample(struct gmk_event_link *link, const uint16_t rows[5], bool quiet);
void gmk_event_reply(struct gmk_event_link *link, uint16_t acknowledged, uint8_t reply[GMK_EVENT_FRAME]);
bool gmk_event_decode(const uint8_t reply[GMK_EVENT_FRAME], uint16_t *sequence, uint8_t *flags, uint16_t rows[5]);
bool gmk_event_notice_due(const struct gmk_event_link *link, uint32_t now);
void gmk_event_notified(struct gmk_event_link *link, uint32_t now);
bool gmk_event_control(struct gmk_event_link *link, const uint16_t rows[5], bool quiet, const uint8_t request[32], uint8_t response[32]);
