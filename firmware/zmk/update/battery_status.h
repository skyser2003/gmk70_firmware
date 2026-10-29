/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../battery/battery.h"
#include <stdbool.h>
#include <stdint.h>

/* Only the configured wired LEFT USB receiver may serve this local snapshot. */
bool gmk_battery_status_packet(const uint8_t request[32]);
void gmk_battery_status_reply(const uint8_t request[32], const struct gmk_battery_status *status, uint8_t response[32]);
