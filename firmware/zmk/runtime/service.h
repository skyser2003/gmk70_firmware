/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     left, wired, usb_configured, writes_ready, peer;
    uint32_t uptime_ms, matrix_events;
} gmk_zmk_service_env_t;

/* Shared by settings and power; updater builds also exclude active uploads. */
bool gmk_zmk_storage_claim(void);
void gmk_zmk_storage_release(void);
bool gmk_zmk_service_idle(void);
