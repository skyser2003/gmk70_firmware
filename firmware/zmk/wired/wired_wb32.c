/* Copyright (C) 2021 Westberry Technology (ChangZhou) Corp., Ltd
 * Copyright 2026 GMK70 contributors
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * WB32 UART register programming derived from Westberry's ChibiOS UARTv1
 * hal_serial_lld.c (Copyright 2021 Westberry Technology Corp.) and pinned
 * WB32FQ95xx CMSIS definitions. QMK protocol attribution: wired_protocol.c.
 */
#include "wired.h"
#include "wired_protocol.h"
#include "gmk70_wired_pins.h"
#include <soc.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/atomic.h>
#include <string.h>

#define UART_BASE 0x40003800U
#define UART_IRQ 30
#define PCLK1_HZ 96000000U
#define REG(offset) (UART_BASE + (offset))
K_MUTEX_DEFINE(wired_lock);
K_MSGQ_DEFINE(uart_rx, 1, 128, 1);
static struct gmk_protocol         protocol;
static struct gmk_peer             peer;
static struct gmk_debounce         debounce;
static struct gmk_wired_stats      counters;
static atomic_t                    rx_errors, rx_fault;
static bool                        left, initialized;
static bool                        have_last_rx;
static uint32_t                    last_rx_seen;
static bool                        have_first;
static uint8_t                     first;
static int64_t                     deadline;
static const struct gmk_wired_pin *row_pins, *col_pins;

static struct wb32_gpio *port_of(struct gmk_wired_pin pin) {
    return (struct wb32_gpio *)(uintptr_t)(WB32_GPIOA_BASE + 0x400U * pin.port);
}
static void pin_mode(struct gmk_wired_pin pin, uint32_t mode, uint32_t pull) {
    if (pin.port == 255) return;
    struct wb32_gpio *p   = port_of(pin);
    unsigned int      key = irq_lock();
    p->cfgmsk             = (uint16_t)~(1U << pin.pin);
    p->otyper             = 0;
    p->pupdr              = pull << (2 * pin.pin);
    p->moder              = mode << (2 * pin.pin);
    p->current            = 1U << (2 * pin.pin);
    p->ospeedr            = 3U << (2 * pin.pin);
    irq_unlock(key);
}
static bool pin_read(struct gmk_wired_pin pin) {
    return (port_of(pin)->idr & (1U << pin.pin)) != 0;
}
static bool mode_now(void) {
    return pin_read((struct gmk_wired_pin){2, 14}) && pin_read((struct gmk_wired_pin){2, 15});
}
static void uart_isr(const void *unused) {
    ARG_UNUSED(unused);
    /* Bounded ISR: enqueue only; protocol and updater never run in IRQ context. */
    for (unsigned n = 0; n < 32; ++n) {
        uint32_t status = sys_read32(REG(0x14));
        if (!(status & 1)) break;
        uint8_t byte = (uint8_t)sys_read32(REG(0));
        if ((status & 0x9e) || k_msgq_put(&uart_rx, &byte, K_NO_WAIT) != 0) {
            atomic_inc(&rx_errors);
            atomic_set(&rx_fault, 1);
        }
    }
}
static void clear_rx(void *ctx) {
    ARG_UNUSED(ctx);
    unsigned key = irq_lock();
    k_msgq_purge(&uart_rx);
    atomic_set(&rx_fault, 0);
    irq_unlock(key);
}
static bool read_uart(void *ctx, uint8_t *out, size_t n) {
    ARG_UNUSED(ctx);
    if (atomic_get(&rx_fault)) return false;
    if (n && have_first) {
        *out++ = first;
        --n;
        have_first = false;
    }
    while (n--) {
        int64_t remaining = deadline - k_uptime_get();
        if (remaining <= 0 || k_msgq_get(&uart_rx, out++, K_MSEC(remaining)) != 0 || atomic_get(&rx_fault)) return false;
    }
    return true;
}
static bool write_uart(void *ctx, const uint8_t *in, size_t n) {
    ARG_UNUSED(ctx);
    while (n--) {
        while (!(sys_read32(REG(0x7c)) & 2)) {
            if (k_uptime_get() >= deadline) return false;
            k_busy_wait(5);
        }
        if (k_uptime_get() >= deadline) return false;
        sys_write32(*in++, REG(0));
    }
    return true;
}
static const struct gmk_wire_io wire = {NULL, read_uart, write_uart, clear_rx};
/* Each framed transaction gets a finite budget, including partial RX/TX. */
static void master_clear(void *ctx) {
    clear_rx(ctx);
    deadline = k_uptime_get() + GMK_WIRE_TIMEOUT_MS;
}
static const struct gmk_wire_io master_wire = {NULL, read_uart, write_uart, master_clear};

bool gmk_wired_is_left(void) {
    return initialized && left;
}
bool gmk_wired_mode(void) {
    return initialized && mode_now();
}
void gmk_wired_set_update_handler(void (*handler)(const uint8_t[32], uint8_t[32])) {
    k_mutex_lock(&wired_lock, K_FOREVER);
    protocol.update = handler;
    k_mutex_unlock(&wired_lock);
}
bool gmk_wired_update(const uint8_t request[32], uint8_t response[32]) {
    memset(response, 0, 32);
    if (!gmk_wired_is_left() || !gmk_wired_mode() || k_is_in_isr()) return false;
    k_mutex_lock(&wired_lock, K_FOREVER);
    bool ok = gmk_protocol_update(&master_wire, request, response);
    if (!ok) ++counters.failures;
    k_mutex_unlock(&wired_lock);
    return ok;
}
void gmk_wired_scan(uint16_t rows[5]) {
    uint16_t raw[5] = {0};
    if (!initialized) {
        memset(rows, 0, 10);
        return;
    }
    /* ROW2COL drives each populated column low, reads pulled-up rows. */
    for (unsigned c = 0; c < 9; ++c) {
        if (col_pins[c].port == 255) continue;
        struct wb32_gpio *p = port_of(col_pins[c]);
        p->bsrr             = 1U << (col_pins[c].pin + 16);
        pin_mode(col_pins[c], 1, 0);
        k_busy_wait(5);
        for (unsigned r = 0; r < 5; ++r)
            if (!pin_read(row_pins[r])) raw[r] |= (uint16_t)(1U << c);
        pin_mode(col_pins[c], 0, 1);
    }
    k_mutex_lock(&wired_lock, K_FOREVER);
    gmk_matrix_debounce(&debounce, raw, k_uptime_get_32());
    if (mode_now())
        memcpy(rows, debounce.stable, 10);
    else
        memset(rows, 0, 10);
    gmk_protocol_matrix(&protocol, rows);
    k_mutex_unlock(&wired_lock);
}
void gmk_wired_poll(void) {
    if (!gmk_wired_is_left()) return;
    uint16_t rows[5];
    k_mutex_lock(&wired_lock, K_FOREVER);
    if (mode_now()) {
        ++counters.polls;
        if (gmk_protocol_poll(&master_wire, rows))
            gmk_peer_set(&peer, rows, k_uptime_get_32());
        else
            ++counters.failures;
    } else
        peer.connected = false;
    k_mutex_unlock(&wired_lock);
}
bool gmk_wired_remote_matrix(uint16_t rows[5]) {
    k_mutex_lock(&wired_lock, K_FOREVER);
    if (!gmk_wired_mode()) peer.connected = false;
    bool ok = gmk_peer_rows(&peer, k_uptime_get_32(), rows);
    k_mutex_unlock(&wired_lock);
    return ok;
}
static void stats_locked(struct gmk_wired_stats *stats) {
    *stats           = counters;
    stats->rx_errors = (uint32_t)atomic_get(&rx_errors);
    stats->uptime_ms = k_uptime_get_32();
    uint16_t rows[5];
    stats->peer = left ? gmk_peer_rows(&peer, stats->uptime_ms, rows) : (have_last_rx && (uint32_t)(stats->uptime_ms - last_rx_seen) < GMK_PEER_TIMEOUT_MS);
    if (!gmk_wired_mode()) stats->peer = false;
}
void gmk_wired_get_stats(struct gmk_wired_stats *stats) {
    k_mutex_lock(&wired_lock, K_FOREVER);
    stats_locked(stats);
    k_mutex_unlock(&wired_lock);
}
bool gmk_wired_try_get_stats(struct gmk_wired_stats *stats) {
    if (k_mutex_lock(&wired_lock, K_NO_WAIT) != 0) return false;
    stats_locked(stats);
    k_mutex_unlock(&wired_lock);
    return true;
}
static void serial_thread(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    while (true) {
        if (!initialized || left) {
            k_sleep(K_MSEC(20));
            continue;
        }
        if (k_msgq_get(&uart_rx, &first, K_FOREVER) != 0) continue;
        if (!initialized || left || !mode_now()) continue;
        k_mutex_lock(&wired_lock, K_FOREVER);
        deadline            = k_uptime_get() + GMK_WIRE_TIMEOUT_MS;
        have_first          = true;
        atomic_val_t errors = atomic_get(&rx_errors);
        if (gmk_protocol_serve(&protocol, &wire) && errors == atomic_get(&rx_errors)) {
            ++counters.transactions;
            have_last_rx = true;
            last_rx_seen = k_uptime_get_32();
        } else {
            ++counters.failures;
            clear_rx(NULL);
        }
        have_first = false;
        k_mutex_unlock(&wired_lock);
    }
}
K_THREAD_DEFINE(gmk_serial_thread, 1536, serial_thread, NULL, NULL, NULL, 4, 0, 0);
static int wired_init(void) {
    sys_set_bits(WB32_RCC_BASE + 0x48, (1U << 15) | (1U << 14) | (1U << 7) | (1U << 6) | (1U << 5));
    pin_mode(gmk_role_pin, 0, 0);
    left = pin_read(gmk_role_pin);
    pin_mode((struct gmk_wired_pin){2, 14}, 0, 1);
    pin_mode((struct gmk_wired_pin){2, 15}, 0, 1);
    row_pins = left ? gmk_left_rows : gmk_right_rows;
    col_pins = left ? gmk_left_cols : gmk_right_cols;
    for (unsigned r = 0; r < 5; ++r)
        pin_mode(row_pins[r], 0, 1);
    for (unsigned c = 0; c < 9; ++c)
        if (col_pins[c].port != 255) {
            port_of(col_pins[c])->bsrr = 1U << col_pins[c].pin;
            pin_mode(col_pins[c], 0, 1);
        }
    /* UART1 A9/A10 AF7, push-pull, 115200 8N1, FIFO enabled. */
    for (unsigned pin = 9; pin <= 10; ++pin) {
        struct gmk_wired_pin p = {0, pin};
        pin_mode(p, 2, 0);
        unsigned key       = irq_lock();
        port_of(p)->cfgmsk = (uint16_t)~(1U << pin);
        port_of(p)->afrh   = 7U << ((pin - 8) * 4);
        irq_unlock(key);
    }
    sys_set_bits(WB32_RCC_BASE + 0x90, 1U << 14);
    sys_clear_bits(WB32_RCC_BASE + 0x90, 1U << 14);
    uint32_t divider = (PCLK1_HZ + 115200 / 2) / 115200;
    sys_write32(0, REG(0x10));
    sys_write32(0x80, REG(0xc));
    sys_write32(divider & 15, REG(0xc0));
    sys_write32((divider >> 4) & 255, REG(0));
    sys_write32((divider >> 12) & 255, REG(4));
    sys_write32(3, REG(0xc));
    sys_write32(7, REG(8));
    sys_write32(0, REG(0x9c));
    sys_write32(1, REG(0x98));
    IRQ_CONNECT(UART_IRQ, 2, uart_isr, NULL, 0);
    irq_enable(UART_IRQ);
    sys_write32(5, REG(4));
    initialized = true;
    return 0;
}
SYS_INIT(wired_init, POST_KERNEL, 80);
