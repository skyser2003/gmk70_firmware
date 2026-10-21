// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { GMK_RADIO_RX_BYTES, GMK_RADIO_UNKNOWN_BYTES, GMK_RADIO_ACKS, GMK_RADIO_VALID_FRAMES, GMK_RADIO_CHECKSUM_ERRORS, GMK_RADIO_RAW_HEADERS, GMK_RADIO_RAW_OVERSIZE, GMK_RADIO_RAW_31, GMK_RADIO_RAW_32, GMK_RADIO_RAW_OTHER, GMK_RADIO_RAW_DISPATCHED, GMK_RADIO_RAW_TX_REQUESTS, GMK_RADIO_COUNTERS };

#ifdef GMK70_RADIO_DIAGNOSTICS
void gmk_radio_diag_init(void);
void gmk_radio_diag_count(uint8_t counter, uint8_t device);
void gmk_radio_diag_header(uint8_t length, uint8_t device);
void gmk_radio_diag_version(uint8_t version);
// Snapshot only: no reset, settings mutation, flash, or new radio requests.
bool gmk_radio_diag_query(uint8_t *packet, uint8_t length, bool allowed, uint32_t uptime);
#else
#    define gmk_radio_diag_init() ((void)0)
#    define gmk_radio_diag_count(counter, device) ((void)0)
#    define gmk_radio_diag_header(length, device) ((void)0)
#    define gmk_radio_diag_version(version) ((void)0)
#endif
