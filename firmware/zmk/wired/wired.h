/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
bool gmk_wired_is_left(void);
bool gmk_wired_mode(void);
/* Raw C14:C15 selector bits, without debounce; zero also means uninitialized. */
uint8_t gmk_host_switch(void);
/* LEFT radio builds retain the physical split cable in every valid host mode.
 * This predicate never grants permission for updater or settings writes. */
bool gmk_split_available(void);
/* Handler executes under the transport mutex in the serial thread. It MUST
 * only queue work / read cached replies, never flash or initiate UART relay.
 * The stats getter is safe under Zephyr's recursive transport mutex. */
void gmk_wired_set_update_handler(void (*handler)(const uint8_t[32], uint8_t[32]));
/* Blocking worker-thread API. Never call in USB/IRQ callbacks or system workq. */
bool gmk_wired_update(const uint8_t request[32], uint8_t response[32]);
bool gmk_wired_split_command(const uint8_t request[32], uint8_t response[32]);
bool gmk_wired_remote_matrix(uint16_t rows[5]);
/* Scanner-facing APIs; local rows use 0..4 on either physical half. */
void gmk_wired_scan(uint16_t rows[5]);
void gmk_wired_poll(void);
/* LEFT encoder detents only; thread context. Clear discards pending motion. */
int  gmk_wired_encoder_next(void);
void gmk_wired_encoder_clear(void);
struct gmk_wired_stats {
    uint32_t polls, failures, transactions, rx_errors, uptime_ms;
    bool     peer;
};
void gmk_wired_get_stats(struct gmk_wired_stats *stats);

/* Nonblocking telemetry: false means contended and leaves *stats unchanged. */
bool gmk_wired_try_get_stats(struct gmk_wired_stats *stats);
/* Local sleep boundary, thread context. Prepare arms local wake sources and
 * stops ordinary LEFT peer polling; explicit split control RPCs remain live.
 * Pause after the handshake additionally arms UART RX and stops local scans. */
bool gmk_wired_power_idle(void);
/* Physical input only; an in-flight power RPC must not advertise a held key.
 * Full UART quiescence is still required by prepare and runtime storage. */
bool gmk_wired_power_input_idle(void);
bool gmk_wired_power_prepare(bool critical);
void gmk_wired_power_pause(bool paused);
bool gmk_wired_power_wake_pending(void);
void gmk_wired_power_resume(void);
