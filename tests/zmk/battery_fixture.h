/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

uint32_t    battery_test_time;
unsigned    battery_test_locks;
static bool left = true;
bool        gmk_wired_is_left(void) {
    return left;
}
#include "../../firmware/zmk/battery/battery.c"
#include "../../firmware/zmk/battery/battery_sensor.c"

enum { UART = 0x40008400U, RCC = 0x40010c00U, PORT_A = 0x40000000U, PORT_C = 0x40000800U };
static uint32_t uart_registers[0x100 / 4], rcc_registers[0x100 / 4], gpio_registers[2][0x34 / 4];
static uint8_t  rx[1024], tx[1024];
static size_t   rx_count, rx_cursor, tx_count;
static unsigned mmio_reads, mmio_writes, receive_reads;
static uint32_t line_error;
static bool     tx_ready, endless_rx, initializing;
static unsigned tx_allowance;

static uint32_t *register_at(uintptr_t address) {
    assert(address % 4 == 0);
    if (address >= UART && address < UART + sizeof(uart_registers)) return &uart_registers[(address - UART) / 4];
    if (address >= RCC && address < RCC + sizeof(rcc_registers)) return &rcc_registers[(address - RCC) / 4];
    if (address >= PORT_A && address < PORT_A + sizeof(gpio_registers[0])) return &gpio_registers[0][(address - PORT_A) / 4];
    if (address >= PORT_C && address < PORT_C + sizeof(gpio_registers[1])) return &gpio_registers[1][(address - PORT_C) / 4];
    assert(!"battery touched an unrelated peripheral");
    return NULL;
}
uint32_t sys_read32(uintptr_t address) {
    assert(!battery_test_locks);
    ++mmio_reads;
    if (address == UART + 0x14) return line_error | (endless_rx || rx_cursor < rx_count ? 1U : 0U);
    if (address == UART + 0x7c) return tx_ready && tx_allowance ? 2U : 0U;
    if (address == UART && !(uart_registers[0x0c / 4] & 0x80)) {
        ++receive_reads;
        assert(endless_rx || rx_cursor < rx_count);
        return endless_rx ? 0x55 : rx[rx_cursor++];
    }
    return *register_at(address);
}
void sys_write32(uint32_t value, uintptr_t address) {
    assert(!battery_test_locks);
    ++mmio_writes;
    if (address == UART && !(uart_registers[0x0c / 4] & 0x80)) {
        assert(tx_count < sizeof(tx) && tx_ready && tx_allowance);
        tx[tx_count++] = (uint8_t)value;
        --tx_allowance;
        return;
    }
    if (address == UART + 8) {
        assert(initializing || !(value & 4)); /* RX recovery preserves TX. */
        if (value & 2) rx_cursor = rx_count;
    }
    for (unsigned p = 0; p < 2; ++p) {
        uintptr_t base = p ? PORT_C : PORT_A;
        if (address < base || address >= base + sizeof(gpio_registers[p])) continue;
        assert(initializing); /* Runtime sampling never changes GPIO mode. */
        unsigned offset = (unsigned)(address - base);
        assert(offset != 0x14 && offset != 0x18); /* No GPIO output latches. */
        if (offset == 0x30) {
            gpio_registers[p][offset / 4] = value;
            return;
        }
        uint32_t result = gpio_registers[p][offset / 4];
        for (unsigned pin = 0; pin < 16; ++pin) {
            if (gpio_registers[p][0x30 / 4] & (1U << pin)) continue;
            if ((offset == 0x20 && pin >= 8) || (offset == 0x24 && pin < 8)) continue;
            unsigned width = offset == 4 ? 1 : offset == 0x20 || offset == 0x24 ? 4 : 2;
            unsigned shift = width * (width == 4 ? pin % 8 : pin);
            uint32_t mask  = ((1U << width) - 1) << shift;
            result         = (result & ~mask) | (value & mask);
        }
        gpio_registers[p][offset / 4] = result;
        return;
    }
    *register_at(address) = value;
}
static void reset_model(bool is_left) {
    memset(uart_registers, 0, sizeof(uart_registers));
    memset(rcc_registers, 0xa5, sizeof(rcc_registers));
    memset(gpio_registers, 0xa5, sizeof(gpio_registers));
    battery_test_time = 0;
    left              = is_left;
    rx_count = rx_cursor = tx_count = 0;
    mmio_reads = mmio_writes = receive_reads = 0;
    line_error                               = 0;
    tx_ready                                 = true;
    endless_rx                               = false;
    tx_allowance                             = UINT32_MAX;
    initializing                             = true;
    assert(battery_init() == 0);
    initializing = false;
}
static void feed(const uint8_t *bytes, size_t count) {
    assert(count <= sizeof(rx));
    memcpy(rx, bytes, count);
    rx_count  = count;
    rx_cursor = 0;
    gmk_battery_poll();
}
static struct gmk_battery_status snapshot(void) {
    struct gmk_battery_status status;
    unsigned                  reads = mmio_reads, writes = mmio_writes;
    gmk_battery_get(&status);
    assert(mmio_reads == reads && mmio_writes == writes && !battery_test_locks);
    return status;
}
