// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stdbool.h>
enum { GMK_UPDATE_RPC = 31 };
typedef void (*slave_callback_t)(uint8_t, const void *, uint8_t, void *);
void transaction_register_rpc(int8_t id, slave_callback_t handler);
bool transaction_rpc_exec(int8_t id, uint8_t in_size, const void *in, uint8_t out_size, void *out);
