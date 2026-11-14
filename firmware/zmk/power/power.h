/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Called only by the existing service/UART owner. Native activity and split
 * callbacks only request transitions; they never stop clocks or write flash. */
void gmk_power_poll(bool service_ready);
void gmk_power_receive(const uint8_t request[32], uint8_t response[32]);
bool gmk_power_sleep_pending(void);
bool gmk_power_usb_powered(void);

/* Service owner only; safe while the settings job owns the receiver storage lease. This
 * deliberately does not attempt to claim that lease or wait for its owner. */
bool gmk_power_runtime_storage_safe(void);
/* Native position/pointing callbacks only invalidate the current session. */
void gmk_power_note_activity(void);
/* Checksummed stock 0x60 notification, dispatched by the UART worker. */
void gmk_power_radio_host(bool resume);
#ifdef CONFIG_GMK70_EVENT_IDLE
uint32_t gmk_power_delay(void);
#endif
