/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "battery_irq_fixture.h"

/* A bounded byte-delivery model, NOT a WB32/radio emulator. A stopped UART
 * cannot receive a byte whose start precedes clock restoration. Restrict the
 * negative cases to >=87 us: at least one WHOLE 115200/8N1 byte has already
 * passed, so even a receiver able to restart during a start bit cannot recover
 * the header. Feed the result to the actual ISR queue and maintained parser. */
static void receive_window(unsigned ready_us, unsigned start_us) {
    reset_model(true);
    wired = true;
    k_msgq_purge(&radio_rx);
    atomic_clear(&radio_rx_fault);
    const uint8_t frame[] = {0x5c, 73, (uint8_t)(0x5c + 73)};
    for (unsigned i = 0; i < sizeof(frame); ++i)
        if ((uint64_t)start_us * 115200U + (uint64_t)i * 10000000U >= (uint64_t)ready_us * 115200U) rx[rx_count++] = frame[i];
    fake_uart_isr(NULL);
    gmk_battery_poll();
}
static void test_stop_uart_feasibility(void) {
    const unsigned latencies[] = {87, 100, 150, 200, 250};
    for (unsigned i = 0; i < sizeof(latencies) / sizeof(*latencies); ++i) {
        unsigned latency = latencies[i];
        receive_window(0, 0); /* UART-safe shallow idle control. */
        assert(snapshot().valid && snapshot().percentage == 73);
        receive_window(latency, 0);
        assert(!snapshot().valid && !snapshot().replies);
        /* A wake-before-data agreement would preserve the same frame. */
        receive_window(latency, latency + 1);
        assert(snapshot().valid && snapshot().percentage == 73);
    }
    puts("runtime STOP feasibility: 87/100/150/200/250 us clock-off counterexamples lose the header; clocked and pre-wake controls decode it (bounded model only)");
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
    test_stop_uart_feasibility();
    puts("event radio UART: ISR queue-only RX, service wake, deadline/retry, sleep mask/restore, overflow and cache passed");
}
