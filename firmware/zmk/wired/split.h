/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>

/* Native behavior commands use the existing bounded board RPC, separately
 * from updater RPC 18. Callbacks only queue; the service worker executes. */
void gmk_native_split_receive(const uint8_t request[32], uint8_t response[32]);
void gmk_native_split_poll(void);
