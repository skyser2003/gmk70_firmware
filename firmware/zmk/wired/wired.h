/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
bool gmk_wired_is_left(void);
bool gmk_wired_mode(void);
/* Handler executes under the transport mutex in the serial thread. It MUST
 * only queue work / read cached replies, never flash or initiate UART relay.
 * The stats getter is safe under Zephyr's recursive transport mutex. */
void gmk_wired_set_update_handler(void (*handler)(const uint8_t[32], uint8_t[32]));
/* Blocking worker-thread API. Never call in USB/IRQ callbacks or system workq. */
bool gmk_wired_update(const uint8_t request[32], uint8_t response[32]);
bool gmk_wired_remote_matrix(uint16_t rows[5]);
/* Scanner-facing APIs; local rows use 0..4 on either physical half. */
void gmk_wired_scan(uint16_t rows[5]);
void gmk_wired_poll(void);
struct gmk_wired_stats {
    uint32_t polls, failures, transactions, rx_errors, uptime_ms;
    bool     peer;
};
void gmk_wired_get_stats(struct gmk_wired_stats *stats);

/* Nonblocking telemetry: false means contended and leaves *stats unchanged. */
bool gmk_wired_try_get_stats(struct gmk_wired_stats *stats);
