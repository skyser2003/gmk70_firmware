// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { USER_SYNC_MMS = 15 };
bool transaction_rpc_exec(int8_t id, uint8_t in_len, const void *in, uint8_t out_len, void *out);
