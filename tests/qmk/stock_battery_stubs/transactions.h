// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
// Deliberately differs from stock's numeric ID: use the current QMK enum.
enum { USER_SYNC_MMS = 37 };
bool transaction_rpc_exec(int8_t id, uint8_t request_length, const void *request, uint8_t response_length, void *response);
