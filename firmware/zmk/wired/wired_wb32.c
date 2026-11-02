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
#include "encoder.h"
#ifdef CONFIG_GMK70_NATIVE_UPDATE_WIRE
#    include "native_update_wire.h"
#endif
#if defined(CONFIG_GMK70_NATIVE_UPDATE_WIRE) && defined(CONFIG_GMK70_NATIVE_UPDATE_LEGACY_BRIDGE)
#    include "native_update_bridge.h"
static struct gmk_update_route update_route;
#endif
#ifdef CONFIG_GMK70_NATIVE_SPLIT
#    include "split.h"
#endif
#include "../lighting/lighting.h"
#include "gmk70_wired_pins.h"
#include <soc.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/drivers/gpio.h>
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
static const struct device *const  gpio_ports[] = {DEVICE_DT_GET(DT_NODELABEL(gpioa)), DEVICE_DT_GET(DT_NODELABEL(gpiob)), DEVICE_DT_GET(DT_NODELABEL(gpioc))};
static struct gpio_callback        row_callbacks[3];
static atomic_t                    scan_pending;
static bool                        scan_idle, scan_irq_ready;
static struct gmk_encoder          encoder;
static struct gpio_callback        encoder_callback;
static bool                        encoder_irq_ready;
static bool                        encoder_ready;

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
static void encoder_wake(const struct device *dev, struct gpio_callback *cb, gpio_port_pins_t pins) {
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    gmk_encoder_sample(&encoder, gmk_native_encoder_sample());
    if (!initialized || !left || !gmk_split_available()) gmk_encoder_clear(&encoder);
}
static void encoder_init(void) {
    encoder_irq_ready = false;
    encoder_ready     = false;
    if (!left) return; /* B6/B7 are a left-only device. */
    if (!gmk_native_encoder_init()) return;
    encoder_ready = true;
    gmk_encoder_init(&encoder, GMK_ENCODER_RESOLUTION);
    if (gmk_encoder_a.port >= ARRAY_SIZE(gpio_ports) || gmk_encoder_a.port != gmk_encoder_b.port) return;
    const struct device *dev = gpio_ports[gmk_encoder_a.port];
    gpio_init_callback(&encoder_callback, encoder_wake, BIT(gmk_encoder_a.pin) | BIT(gmk_encoder_b.pin));
    if (gpio_add_callback(dev, &encoder_callback)) return;
    if (!gpio_pin_interrupt_configure(dev, gmk_encoder_a.pin, GPIO_INT_EDGE_BOTH) && !gpio_pin_interrupt_configure(dev, gmk_encoder_b.pin, GPIO_INT_EDGE_BOTH)) {
        encoder_irq_ready = true;
    } else {
        gpio_pin_interrupt_configure(dev, gmk_encoder_a.pin, GPIO_INT_DISABLE);
        gpio_pin_interrupt_configure(dev, gmk_encoder_b.pin, GPIO_INT_DISABLE);
    }
}
int gmk_wired_encoder_next(void) {
    unsigned key       = irq_lock();
    int      direction = 0;
    if (initialized && left && encoder_ready) {
        if (!encoder_irq_ready) gmk_encoder_sample(&encoder, gmk_native_encoder_sample());
        if (gmk_split_available())
            direction = gmk_encoder_take(&encoder);
        else
            gmk_encoder_clear(&encoder);
    }
    irq_unlock(key);
    return direction;
}
void gmk_wired_encoder_clear(void) {
    unsigned key = irq_lock();
    gmk_encoder_clear(&encoder);
    irq_unlock(key);
}

static void matrix_wake(const struct device *dev, struct gpio_callback *callback, gpio_port_pins_t pins) {
    ARG_UNUSED(dev);
    ARG_UNUSED(callback);
    ARG_UNUSED(pins);
    /* IRQs only latch activity; scanning, debounce and reports stay in thread context. */
    atomic_set(&scan_pending, 1);
}

static void matrix_irq_disable(void) {
    for (unsigned r = 0; r < 5; ++r)
        if (row_pins[r].port < ARRAY_SIZE(gpio_ports)) gpio_pin_interrupt_configure(gpio_ports[row_pins[r].port], row_pins[r].pin, GPIO_INT_DISABLE);
}

static bool matrix_irq_init(void) {
    for (unsigned r = 0; r < 5; ++r)
        if (row_pins[r].port >= ARRAY_SIZE(gpio_ports)) return false;
    for (unsigned port = 0; port < ARRAY_SIZE(gpio_ports); ++port) {
        gpio_port_pins_t pins = 0;
        for (unsigned r = 0; r < 5; ++r)
            if (row_pins[r].port == port) pins |= BIT(row_pins[r].pin);
        if (!pins) continue;
        if (!device_is_ready(gpio_ports[port])) return false;
        gpio_init_callback(&row_callbacks[port], matrix_wake, pins);
        if (gpio_add_callback(gpio_ports[port], &row_callbacks[port])) return false;
    }
    return true;
}

static void matrix_idle_end(void) {
    unsigned key = irq_lock();
    matrix_irq_disable();
    for (unsigned c = 0; c < 9; ++c)
        pin_mode(col_pins[c], 0, 1);
    scan_idle = false;
    atomic_set(&scan_pending, 0);
    irq_unlock(key);
}

static void matrix_idle_begin(void) {
    if (!scan_irq_ready) return;
    unsigned key = irq_lock();
    atomic_set(&scan_pending, 0);
    /* Arm before driving columns. Read every row afterwards as well: a key
     * already down, or pressed across this transition, must not need a new edge. */
    for (unsigned r = 0; r < 5; ++r)
        if (gpio_pin_interrupt_configure(gpio_ports[row_pins[r].port], row_pins[r].pin, GPIO_INT_EDGE_FALLING)) {
            scan_irq_ready = false;
            matrix_irq_disable();
            irq_unlock(key);
            return; /* Preserve ordinary polling if interrupts cannot be armed. */
        }
    for (unsigned c = 0; c < 9; ++c) {
        struct gmk_wired_pin pin = col_pins[c];
        if (pin.port == 255) continue;
        struct wb32_gpio *p = port_of(pin);
        p->bsrr             = BIT(pin.pin + 16);
        pin_mode(pin, 1, 0);
        /* All idle outputs sink through open drains, as in the board's QMK wake path. */
        p->otyper = BIT(pin.pin);
    }
    k_busy_wait(5);
    for (unsigned r = 0; r < 5; ++r)
        if (!pin_read(row_pins[r])) atomic_set(&scan_pending, 1);
    scan_idle = true;
    irq_unlock(key);
}
static bool mode_now(void) {
    return gmk_host_switch() == 3;
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
uint8_t gmk_host_switch(void) {
    if (!initialized) return 0;
    return (uint8_t)((pin_read((struct gmk_wired_pin){2, 14}) ? 2 : 0) | (pin_read((struct gmk_wired_pin){2, 15}) ? 1 : 0));
}
bool gmk_split_available(void) {
#ifdef CONFIG_GMK70_RADIO
    if (gmk_wired_is_left()) return gmk_host_switch() != 0;
#endif
    return gmk_wired_mode();
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
#if defined(CONFIG_GMK70_NATIVE_UPDATE_WIRE) && defined(CONFIG_GMK70_NATIVE_UPDATE_LEGACY_BRIDGE)
    bool ok = gmk_native_update_bridge_exchange(&master_wire, &update_route, request, response);
#elif defined(CONFIG_GMK70_NATIVE_UPDATE_WIRE)
    bool ok = gmk_native_update_exchange(&master_wire, request, response);
#else
    bool ok = gmk_protocol_update(&master_wire, request, response);
#endif
    if (!ok) ++counters.failures;
    k_mutex_unlock(&wired_lock);
    return ok;
}
bool gmk_wired_split_command(const uint8_t request[32], uint8_t response[32]) {
    memset(response, 0, 32);
    if (!gmk_wired_is_left() || !gmk_split_available() || k_is_in_isr()) return false;
    k_mutex_lock(&wired_lock, K_FOREVER);
    bool ok = gmk_protocol_split_command(&master_wire, request, response);
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
    /* A quiet matrix stays electrically armed for a GPIO edge. Keep split,
     * host-mode gates and lighting alive without another local GPIO sweep. */
    if (scan_idle && !atomic_get(&scan_pending)) goto publish;
    if (scan_idle) matrix_idle_end();
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
    bool active = false;
    for (unsigned r = 0; r < 5; ++r)
        active |= (debounce.candidate[r] | debounce.stable[r]) != 0;
    k_mutex_unlock(&wired_lock);
    if (!active) matrix_idle_begin();
publish:
    k_mutex_lock(&wired_lock, K_FOREVER);
    if (gmk_split_available())
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
    if (gmk_split_available()) {
        ++counters.polls;
        if (gmk_protocol_poll(&master_wire, rows)) {
            gmk_peer_set(&peer, rows, k_uptime_get_32());
            gmk_lighting_wire_poll(&master_wire);
        } else
            ++counters.failures;
    } else
        peer.connected = false;
    k_mutex_unlock(&wired_lock);
}
bool gmk_wired_remote_matrix(uint16_t rows[5]) {
    k_mutex_lock(&wired_lock, K_FOREVER);
    if (!gmk_split_available()) peer.connected = false;
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
    if (!gmk_split_available()) stats->peer = false;
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
static bool serve_one(void) {
#ifdef CONFIG_GMK70_NATIVE_UPDATE_WIRE
    if (first == 'Z') return gmk_native_update_serve(&wire, protocol.update);
#endif
    return gmk_protocol_serve(&protocol, &wire);
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
        if (serve_one() && errors == atomic_get(&rx_errors)) {
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
    protocol.lighting = gmk_lighting_receive;
#ifdef CONFIG_GMK70_NATIVE_SPLIT
    protocol.split_command = gmk_native_split_receive;
#endif
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
    scan_idle = false;
    atomic_set(&scan_pending, 0);
    scan_irq_ready = matrix_irq_init();
    encoder_init();
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
