/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_UART_INTERRUPT_DRIVEN 1
#include "../../dependencies/zmk/app/src/studio/uart_rpc_transport.c"
#include "../../dependencies/zmk/app/src/studio/msg_framing.h"
struct device          test_uart;
static struct ring_buf rx = {.size = TEST_RX_CAPACITY}, tx;
static uint8_t         incoming[512];
static unsigned        count, at, notified;
struct ring_buf       *zmk_rpc_get_rx_buf(void) {
    return &rx;
}
struct ring_buf *zmk_rpc_get_tx_buf(void) {
    return &tx;
}
void zmk_rpc_rx_notify(void) {
    ++notified;
}
uint32_t ring_buf_put_claim(struct ring_buf *b, uint8_t **p, uint32_t n) {
    if (n > b->size - b->count) n = b->size - b->count;
    *p = b->data + b->count;
    return n;
}
int ring_buf_put_finish(struct ring_buf *b, uint32_t n) {
    assert(b->count + n <= b->size);
    b->count += n;
    return 0;
}
int uart_irq_update(const struct device *dev) {
    (void)dev;
    return 1;
}
int uart_irq_rx_ready(const struct device *dev) {
    (void)dev;
    return 1;
}
int uart_irq_tx_ready(const struct device *dev) {
    (void)dev;
    return 0;
}
int uart_fifo_read(const struct device *dev, uint8_t *out, int n) {
    (void)dev;
    if ((unsigned)n > count - at) n = count - at;
    memcpy(out, incoming + at, n);
    at += n;
    return n;
}
int uart_fifo_fill(const struct device *dev, const uint8_t *data, int n) {
    (void)dev;
    (void)data;
    return n;
}
int main(void) {
    count = fread(incoming, 1, sizeof(incoming), stdin);
    assert(count && count <= sizeof(incoming));
    assert(!uart_rpc_interface_init());
    serial_cb(&test_uart, NULL);
    assert(notified == 1);
    if (rx.count != count) {
        fprintf(stderr, "actual pinned CDC callback discarded %u of %u framed bytes (RX capacity %u)\n", count - rx.count, count, rx.size);
        return 1;
    }
    assert(at == count);
    assert(!memcmp(rx.data, incoming, count));
    enum studio_framing_state state = FRAMING_STATE_IDLE;
    for (unsigned i = 0; i < count; ++i)
        studio_framing_process_byte(&state, rx.data[i]);
    assert(state == FRAMING_STATE_EOF);
    return 0;
}
