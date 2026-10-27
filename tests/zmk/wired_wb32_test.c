/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <zephyr/kernel.h>
#include "../../firmware/zmk/wired/wired_wb32.c"
int64_t fake_ms;
void (*fake_uart_isr)(const void *);
static uint8_t  hw_rx[64];
static size_t   hw_pos, hw_size;
static uint32_t hw_error;
static bool     saw_divider;
uint32_t        sys_read32(uintptr_t address) {
    if (address == REG(0x14)) return hw_error | (hw_pos < hw_size ? 1 : 0);
    if (address == REG(0)) {
        assert(hw_pos < hw_size);
        return hw_rx[hw_pos++];
    }
    return *(uint32_t *)address;
}
void sys_write32(uint32_t value, uintptr_t address) {
    if (address == REG(0) && *(uint32_t *)REG(0xc) == 0x80) {
        assert(value == 52);
        saw_divider = true;
    }
    *(uint32_t *)address = value;
}
int k_msgq_get(struct k_msgq *q, void *out, int timeout) {
    if (!q->count) {
        if (timeout > 0) fake_ms += timeout;
        return -EAGAIN;
    }
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
    /* Fake MMIO is anonymous process RAM at the vendor register addresses. */
    assert(mmap((void *)0x40000000, 0x20000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)0x40000000);
    struct wb32_gpio *a = (void *)WB32_GPIOA_BASE, *b = (void *)WB32_GPIOB_BASE, *c = (void *)WB32_GPIOC_BASE;
    b->idr = 1U << 9;
    c->idr = 0xffff;
    a->idr = 0xffff;
    assert(wired_init() == 0);
    assert(gmk_wired_is_left() && gmk_wired_mode());
    assert(saw_divider && *(uint32_t *)REG(0xc0) == 1 && *(uint32_t *)REG(0xc) == 3 && *(uint32_t *)REG(4) == 5);
    assert(*(uint32_t *)REG(8) == 7 && *(uint32_t *)REG(0x98) == 1 && *(uint32_t *)REG(0x9c) == 0);
    assert((sys_read32(WB32_RCC_BASE + 0x48) & 0xc0e0) == 0xc0e0);
    uint8_t out[4];
    hw_rx[0] = 10;
    hw_rx[1] = 11;
    hw_size  = 2;
    fake_uart_isr(NULL);
    assert(uart_rx.count == 2);
    deadline = fake_ms + 20;
    assert(read_uart(NULL, out, 2));
    assert(out[0] == 10 && out[1] == 11);
    deadline      = fake_ms + 20;
    int64_t start = fake_ms;
    assert(!read_uart(NULL, out, 1));
    assert(fake_ms - start == 20);
    hw_pos   = 0;
    hw_size  = 1;
    hw_error = 8;
    fake_uart_isr(NULL);
    assert(atomic_get(&rx_fault));
    deadline = fake_ms + 20;
    assert(!read_uart(NULL, out, 1));
    clear_rx(NULL);
    hw_error = 0;
    assert(!atomic_get(&rx_fault));
    for (unsigned i = 0; i < 128; ++i)
        assert(k_msgq_put(&uart_rx, &out[0], K_NO_WAIT) == 0);
    hw_pos  = 0;
    hw_size = 1;
    fake_uart_isr(NULL);
    assert(atomic_get(&rx_fault));
    clear_rx(NULL);
    *(uint32_t *)REG(0x7c) = 0;
    deadline               = fake_ms + 20;
    start                  = fake_ms;
    assert(!write_uart(NULL, out, 1));
    assert(fake_ms - start == 20);
    *(uint32_t *)REG(0x7c) = 2;
    deadline               = fake_ms + 20;
    assert(write_uart(NULL, out, 1));
    uint16_t rows[5];
    a->idr &= ~(1U << 1);
    gmk_wired_scan(rows);
    assert(rows[0] == 0);
    fake_ms += 5;
    gmk_wired_scan(rows);
    assert(rows[0] == 0xff);
    c->idr &= ~(1U << 14);
    gmk_wired_scan(rows);
    for (unsigned i = 0; i < 5; ++i)
        assert(rows[i] == 0);
    uint8_t request[32] = {0}, response[32];
    memset(response, 99, 32);
    assert(!gmk_wired_update(request, response));
    for (unsigned i = 0; i < 32; ++i)
        assert(response[i] == 0);
    c->idr |= 1U << 14;
    b->idr = 0;
    memset(&debounce, 0, sizeof(debounce));
    assert(wired_init() == 0);
    assert(!gmk_wired_is_left());
    gmk_wired_scan(rows);
    fake_ms += 5;
    gmk_wired_scan(rows);
    assert(rows[0] == 0x1ff);
    assert(!gmk_wired_update(request, response));
    assert(munmap((void *)0x40000000, 0x20000) == 0);
    puts("native real WB32 UART MMIO/IRQ/finite timeout/role/matrix tests passed");
}
