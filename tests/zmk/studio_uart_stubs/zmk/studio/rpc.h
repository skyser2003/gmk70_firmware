#pragma once
#include <zephyr/sys/ring_buffer.h>
struct ring_buf *zmk_rpc_get_rx_buf(void);
struct ring_buf *zmk_rpc_get_tx_buf(void);
void             zmk_rpc_rx_notify(void);
#define ZMK_RPC_TRANSPORT(name, type, start, stop, reset, notify) static void *refs[] __attribute__((used)) = {(void *)&start, (void *)&stop, (void *)&notify}
