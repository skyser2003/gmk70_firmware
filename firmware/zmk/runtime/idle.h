/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#ifdef CONFIG_GMK70_EVENT_IDLE
/* Scheduling only. These functions MUST NOT generate ZMK activity events. */
void gmk_scan_notify(void);
void gmk_service_notify(void);
void gmk_scan_wait(uint32_t maximum_ms);
void gmk_service_wait(uint32_t maximum_ms);
#else
static inline void gmk_scan_notify(void) {}
static inline void gmk_service_notify(void) {}
#endif
