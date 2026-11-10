/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "battery_fixture.h"
void (*fake_uart_isr)(const void *);
static unsigned notifications;
void            gmk_service_notify(void) {
    ++notifications;
}
void gmk_scan_notify(void) {}
int  k_msgq_get(struct k_msgq *q, void *out, int timeout) {
    (void)timeout;
    if (!q->count) return -EAGAIN;
    *(uint8_t *)out = q->bytes[q->head++ % 128];
    --q->count;
    return 0;
}
int k_msgq_put(struct k_msgq *q, const void *in, int timeout) {
    (void)timeout;
    if (q->count == 128) return -ENOSPC;
    q->bytes[q->tail++ % 128] = *(const uint8_t *)in;
    ++q->count;
    return 0;
}
void k_msgq_purge(struct k_msgq *q) {
    q->count = q->head = q->tail = 0;
}
int main(void) {
    reset_model(true);
    wired = true;
    assert(fake_uart_isr && uart_registers[1] == 5);
    assert(gmk_battery_delay() == 1000);
    const uint8_t bytes[] = {0x5c, 73, (uint8_t)(0x5c + 73)};
    memcpy(rx, bytes, sizeof(bytes));
    rx_count        = sizeof(bytes);
    unsigned writes = mmio_writes;
    fake_uart_isr(NULL);
    assert(!state.status.replies);
    assert(mmio_writes == writes && notifications == 1 && radio_rx.count == 3);
    assert(!gmk_battery_idle() && gmk_battery_delay() == 1);
    assert(!gmk_battery_power_quiesce(true));
    gmk_battery_poll();
    assert(snapshot().percentage == 73 && snapshot().replies == 1);
    assert(radio_rx.count == 0 && gmk_battery_idle());
    assert(gmk_battery_power_quiesce(true) && !uart_registers[1]);
    assert(gmk_battery_delay() == UINT32_MAX);
    assert(gmk_battery_power_quiesce(false) && uart_registers[1] == 5);
    gmk_battery_request();
    assert(notifications == 2 && gmk_battery_delay() == 1);
    gmk_battery_poll();
    assert(state.waiting_ack && gmk_battery_delay() == 10);
    battery_test_time = 9;
    assert(gmk_battery_delay() == 1);
    battery_test_time = 10;
    gmk_battery_poll(); /* Original bounded retry. */
    assert(state.retry_used);
    k_msgq_purge(&radio_rx);
    for (unsigned n = 0; n < 128; ++n)
        assert(!k_msgq_put(&radio_rx, bytes, 0));
    rx_cursor = 0;
    fake_uart_isr(NULL);
    assert(atomic_get(&radio_rx_fault));
    gmk_battery_poll();
    assert(snapshot().errors);
    /* Exercise read-only cache and the legacy fixture helper without hardware. */
    feed(bytes, 0);
    puts("event radio UART: ISR queue-only RX, service wake, deadline/retry, sleep mask/restore, overflow and cache passed");
}
