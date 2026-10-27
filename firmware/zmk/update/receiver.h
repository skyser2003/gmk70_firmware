/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "protocol.h"

typedef struct {
    bool     left, wired, usb_configured, watchdog_ready, peer;
    uint32_t uptime_ms, feed_calls, matrix_events;
} gmk_zmk_update_env_t;

/* Initialization precedes registration of either transport callback. */
void gmk_zmk_receiver_init(const gmk_zmk_update_env_t *env);
/* Nonblocking: only validates, queues, or copies cached state. */
void gmk_zmk_receiver_receive(const uint8_t request[32], uint8_t response[32], bool usb, const gmk_zmk_update_env_t *env);
/* Exactly one worker owns this entry point, common state, flash and relay. */
void gmk_zmk_receiver_step(const gmk_zmk_update_env_t *env);
/* Platform reset, used only by the dedicated worker after the ACK grace. */
void gmk_zmk_update_reset(void);
