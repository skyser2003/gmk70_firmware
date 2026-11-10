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
#include "../runtime/idle.h"
#ifdef CONFIG_GMK70_EVENT_IDLE
#    include "event_link.h"
static struct gmk_event_link event_link;
static bool                  event_mode, event_probed, event_armed, master_ack;
static uint16_t              event_ack;
static uint32_t              event_poll_at, event_session, event_rx_at;
static atomic_t              event_notice;
static bool                  event_fast;
static uint8_t               event_failures, event_tag;
static atomic_t              master_receiving;
#endif

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
static atomic_t                    power_wake;
static bool                        power_prepared, power_paused;
static uint8_t                     power_inputs;
static uint8_t                     power_armed;
static bool                        power_critical;
#ifdef CONFIG_GMK70_EVENT_IDLE
static struct gpio_callback mode_callback;
static bool                 mode_irq_ready;
static void                 mode_wake(const struct device *dev, struct gpio_callback *callback, gpio_port_pins_t pins) {
    ARG_UNUSED(dev);
    ARG_UNUSED(callback);
    ARG_UNUSED(pins);
    if (power_prepared) atomic_set(&power_wake, 1);
    gmk_service_notify();
    gmk_scan_notify();
}
static void mode_irq_arm(void) {
    const struct device *dev = gpio_ports[2];
    mode_irq_ready           = !gpio_pin_interrupt_configure(dev, 14, GPIO_INT_EDGE_BOTH) && !gpio_pin_interrupt_configure(dev, 15, GPIO_INT_EDGE_BOTH);
}
uint32_t gmk_wired_control_delay(void) {
    return mode_irq_ready ? 1000 : 50;
}
#endif

#define POWER_LOCAL_LINES (BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(6) | BIT(7) | BIT(13) | BIT(14) | BIT(15))
#define EXTI_PENDING_ADDRESS 0x40001814U
#define EXTI_MASK_ADDRESS 0x40001800U
enum { POWER_ARM_A7 = 1, POWER_ARM_A10 = 2, POWER_ARM_C14 = 4, POWER_ARM_C15 = 8, POWER_ARM_B6 = 16, POWER_RESTORE_ENCODER = 32 };

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
    if (power_prepared) {
        atomic_set(&power_wake, 1);
        return;
    }
    gmk_scan_notify();
    if (!initialized || !left || !encoder_ready) return;
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
    if (!power_paused && initialized && left && encoder_ready) {
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
    gmk_scan_notify();
    gmk_service_notify();
    if (power_prepared) atomic_set(&power_wake, 1);
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
        if (power_paused && (status & 0x9fU)) atomic_set(&power_wake, 1);
        if (!(status & 1)) break;
        uint8_t byte = (uint8_t)sys_read32(REG(0));
        if ((status & 0x9e) || k_msgq_put(&uart_rx, &byte, K_NO_WAIT) != 0) {
            atomic_inc(&rx_errors);
            atomic_set(&rx_fault, 1);
        }
#ifdef CONFIG_GMK70_EVENT_IDLE
        if (left) {
            /* Payloads may contain every byte value. Only bytes arriving
             * outside a master exchange can be unsolicited notices. */
            if (!atomic_get(&master_receiving)) {
                if (byte == GMK_EVENT_NOTICE) atomic_set(&event_notice, 1);
                gmk_scan_notify();
            }
        }
#endif
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
#ifdef CONFIG_GMK70_EVENT_IDLE
    if (left && master_ack && n) {
        master_ack = false;
        uint8_t value;
        do {
            int64_t remaining = deadline - k_uptime_get();
            if (remaining <= 0 || k_msgq_get(&uart_rx, &value, K_MSEC(remaining)) || atomic_get(&rx_fault)) return false;
        } while (value == GMK_EVENT_NOTICE);
        *out++ = value;
        --n;
    }
#endif
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
#ifdef CONFIG_GMK70_EVENT_IDLE
    /* Native updater frames have no legacy ACK byte. All payload bytes,
     * including 0xfc, must pass unchanged in both carriers. */
    if (left && master_ack && n != 1) master_ack = false;
#endif
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
#ifdef CONFIG_GMK70_EVENT_IDLE
    master_ack = true;
    atomic_set(&master_receiving, 1);
#endif
}
static const struct gmk_wire_io master_wire = {NULL, read_uart, write_uart, master_clear};

#ifdef CONFIG_GMK70_EVENT_IDLE
static void event_legacy(void) {
    event_link.enabled = event_armed = false;
}
static bool event_control(const uint8_t request[32], uint8_t response[32]) {
    if (memcmp(request, "EVT\1", 4)) return false;
    if (left || power_prepared || power_paused) return true;
    uint32_t prior   = event_link.token;
    bool     enabled = event_link.enabled;
    (void)gmk_event_control(&event_link, debounce.stable, scan_idle, request, response);
    if (response[5] == 1 && request[4]) {
        if (!enabled || prior != event_link.token) event_armed = false;
        event_rx_at = k_uptime_get_32();
    }
    return true;
}
static bool event_read(const uint8_t request[4], uint8_t response[16]) {
    if (!event_link.enabled || left || power_prepared || power_paused) return false;
    if (request[2] != event_link.tag || request[3] != gmk_wire_crc8(request, 3)) return false;
    event_armed = true; /* A lost enable reply cannot start unsolicited TX. */
    event_rx_at = k_uptime_get_32();
    gmk_event_reply(&event_link, (uint16_t)(request[0] | ((uint16_t)request[1] << 8)), response);
    gmk_scan_notify();
    return true;
}
/* 1 ready, 0 ambiguous/retry, -1 a completed legacy zero reply. */
static int negotiate_events(bool resume) {
    uint8_t  request[32] = {'E', 'V', 'T', 1}, response[32];
    uint32_t token       = resume ? event_session : ++event_session;
    if (!token) token = ++event_session;
    bool retained = false;
    for (unsigned i = 0; i < 4; ++i)
        request[8 + i] = (uint8_t)(token >> (8 * i));
    for (unsigned step = 0; step < 2; ++step) {
        request[4]  = (uint8_t)step;
        request[31] = gmk_wire_crc8(request, 31);
        if (!gmk_protocol_split_command(&master_wire, request, response)) return 0;
        uint8_t nonzero = 0;
        for (unsigned i = 0; i < sizeof(response); ++i)
            nonzero |= response[i];
        if (!nonzero) return -1;
        if (response[5] != 1 || response[6] > 1 || response[31] != gmk_wire_crc8(response, 31)) return 0;
        if (!step)
            retained = resume && response[6];
        else if (!response[6])
            return 0;
        response[5] = response[6] = 0;
        response[31]              = request[31];
        if (memcmp(request, response, 32)) return 0;
    }
    if (!retained) event_ack = 0;
    event_fast = true;
    event_tag  = (uint8_t)(gmk_wire_crc8(request + 8, 4) | 1U);
    return 1;
}
static uint32_t remaining_ms(uint32_t now, uint32_t then, uint32_t interval) {
    uint32_t elapsed = now - then;
    return elapsed >= interval ? 0 : interval - elapsed;
}
#endif

/* Retain the role probe for the linked settings-admission check under LTO. */
__attribute__((noipa)) bool gmk_wired_is_left(void) {
    return initialized && left;
}
bool gmk_wired_mode(void) {
    return initialized && mode_now();
}
uint8_t gmk_host_switch(void) {
    if (!initialized) return 0;
    return (uint8_t)((pin_read((struct gmk_wired_pin){2, 14}) ? 2 : 0) | (pin_read((struct gmk_wired_pin){2, 15}) ? 1 : 0));
}

static uint8_t power_sample_inputs(void) {
    return gmk_host_switch() | (pin_read((struct gmk_wired_pin){0, 7}) ? BIT(2) : 0) | (pin_read((struct gmk_wired_pin){0, 10}) ? BIT(3) : 0) | (pin_read((struct gmk_wired_pin){1, 6}) ? BIT(4) : 0) | (left && pin_read((struct gmk_wired_pin){1, 7}) ? BIT(5) : 0);
}

static bool power_rows_released(void) {
    for (unsigned r = 0; r < 5; ++r)
        if (!pin_read(row_pins[r])) return false;
    return true;
}

static bool power_uart_idle(void) {
    /* Pinned WB32 UART_LSR: DR0, errors1..4/7, TEMT6. TEMT covers both FIFO
     * and the physical shift register; TFNF alone only allows another byte. */
    uint32_t status = sys_read32(REG(0x14));
    return !have_first && !k_msgq_num_used_get(&uart_rx) && !atomic_get(&rx_fault) && (status & 0xdfU) == BIT(6) && pin_read((struct gmk_wired_pin){0, 10});
}

static bool power_input_idle(void) {
    if (!initialized || !scan_idle || !scan_irq_ready || atomic_get(&scan_pending) || (left && encoder.count)) return false;
    for (unsigned r = 0; r < 5; ++r)
        if (debounce.candidate[r] || debounce.stable[r]) return false;
#ifdef CONFIG_GMK70_EVENT_IDLE
    /* Queued input must reach LEFT before a quiet physical matrix can sleep. */
    if (!left && event_link.enabled && event_link.count) return false;
#endif
    return true;
}

bool gmk_wired_power_input_idle(void) {
    /* Single-core snapshot: scanner mutations do not yield, and the IRQ mask
     * also excludes encoder updates. A required peer RPC may own wired_lock
     * while waiting for UART; that is not a held key or new physical input. */
    unsigned key  = irq_lock();
    bool     idle = power_input_idle();
    irq_unlock(key);
    return idle;
}

static bool power_idle_locked(void) {
    if (!power_input_idle() || !power_uart_idle()) return false;
    return !power_prepared || !gmk_wired_power_wake_pending();
}

bool gmk_wired_power_idle(void) {
    if (k_is_in_isr() || k_mutex_lock(&wired_lock, K_NO_WAIT)) return false;
    unsigned key  = irq_lock();
    bool     idle = power_idle_locked();
    irq_unlock(key);
    k_mutex_unlock(&wired_lock);
    return idle;
}

bool gmk_wired_power_wake_pending(void) {
    if (atomic_get(&power_wake)) return true;
    if (!power_prepared) return false;
    uint32_t lines = (power_critical ? BIT(6) | BIT(7) | BIT(14) | BIT(15) : POWER_LOCAL_LINES) | (power_paused ? BIT(10) : 0);
    uint8_t  mask  = (power_paused ? 0x3fU : 0x37U) & (power_critical ? 0x1fU : 0x3fU);
    if ((sys_read32(EXTI_PENDING_ADDRESS) & lines) || ((power_sample_inputs() ^ power_inputs) & mask) || (!power_critical && !power_rows_released()) || (power_paused && !power_uart_idle())) atomic_set(&power_wake, 1);
    return atomic_get(&power_wake) != 0;
}

static bool power_arm(unsigned port, unsigned pin) {
    return gpio_pin_interrupt_configure(gpio_ports[port], pin, GPIO_INT_EDGE_BOTH) == 0;
}

static bool power_arm_free(unsigned port, unsigned pin, uint8_t owned) {
    /* GPIO's EXTI arbiter owns ports, not clients on the same port. Refuse
     * every preexisting extra line rather than commandeering another client. */
    if (sys_read32(EXTI_MASK_ADDRESS) & BIT(pin)) {
#ifdef CONFIG_GMK70_EVENT_IDLE
        /* Adopt our persistent mode-switch IRQs in place. Reconfiguring them
         * clears a pending edge and would both lose wake and block every sleep.
         * A different port/trigger remains another owner's line. */
        if (mode_irq_ready && port == 2 && (pin == 14 || pin == 15) && ((sys_read32(0x40001408U + 4U * (pin / 4U)) >> (4U * (pin % 4U))) & 15U) == port && (sys_read32(0x40001808U) & BIT(pin)) && (sys_read32(0x4000180cU) & BIT(pin))) return true;
#endif
        return false;
    }
    if (!power_arm(port, pin)) return false;
    power_armed |= owned;
    return true;
}

/* Preserve the linked safety boundary under LTO. */
__attribute__((noipa)) bool gmk_wired_power_prepare(bool critical) {
    if (k_is_in_isr() || k_mutex_lock(&wired_lock, K_NO_WAIT)) return false;
    unsigned key = irq_lock();
    bool     ok;
    if (power_prepared) {
        ok = !gmk_wired_power_wake_pending();
        goto out; /* RPC traffic remains valid until the final UART pause. */
    }
    ok = critical ? initialized && power_uart_idle() : power_idle_locked();
    if (!ok) goto out;
    /* Existing matrix row IRQs are already armed. Do not reconfigure them or
     * clear their pending bits. Clear only our previous software latch before
     * local arming begins; no later stage may discard a newly observed edge. */
    atomic_set(&power_wake, 0);
    power_prepared         = true;
    power_critical         = critical;
    uint32_t pending_lines = critical ? BIT(6) | BIT(14) | BIT(15) : POWER_LOCAL_LINES;
    if (sys_read32(EXTI_PENDING_ADDRESS) & sys_read32(EXTI_MASK_ADDRESS) & pending_lines) atomic_set(&power_wake, 1);
    if (critical) {
        /* Canonical QMK critical policy excludes the held matrix: columns
         * become pulled-up inputs and rows pulled-down inputs, no row IRQs. */
        matrix_irq_disable();
        for (unsigned c = 0; c < 9; ++c)
            pin_mode(col_pins[c], 0, 1);
        for (unsigned r = 0; r < 5; ++r)
            pin_mode(row_pins[r], 0, 2);
        scan_idle = false;
    }
    bool b6_armed = left && encoder_irq_ready;
    power_armed   = b6_armed ? POWER_RESTORE_ENCODER : 0;
    pin_mode((struct gmk_wired_pin){0, 7}, 0, 0);
    if (!left) pin_mode((struct gmk_wired_pin){1, 6}, 0, 1);
    power_inputs = power_sample_inputs();
    row_callbacks[0].pin_mask |= BIT(7) | BIT(10);
    row_callbacks[2].pin_mask |= BIT(14) | BIT(15);
    gpio_init_callback(&encoder_callback, encoder_wake, BIT(6));
    if (gpio_add_callback(gpio_ports[1], &encoder_callback)) ok = false;
    /* A7 cable and LEFT encoder A=B7 share EXTI7. Preserve a preexisting
     * encoder edge before yielding this line; B6 remains a knob wake source. */
    if (b6_armed) {
        if (!critical && (sys_read32(EXTI_PENDING_ADDRESS) & BIT(7))) atomic_set(&power_wake, 1);
        gpio_pin_interrupt_configure(gpio_ports[1], 7, GPIO_INT_DISABLE);
        encoder_irq_ready = false;
    }
    if ((!b6_armed && !power_arm_free(1, 6, POWER_ARM_B6)) || !power_arm_free(0, 7, POWER_ARM_A7) || !power_arm_free(2, 14, POWER_ARM_C14) || !power_arm_free(2, 15, POWER_ARM_C15)) ok = false;
    if (gmk_wired_power_wake_pending()) ok = false;
out:
    irq_unlock(key);
    k_mutex_unlock(&wired_lock);
    if (!ok && power_prepared) gmk_wired_power_resume();
    return ok;
}

void gmk_wired_power_pause(bool paused) {
    unsigned key = irq_lock();
    power_paused = paused;
    if (paused) {
        if (!power_prepared || (!(power_armed & POWER_ARM_A10) && !power_arm_free(0, 10, POWER_ARM_A10))) atomic_set(&power_wake, 1);
        (void)gmk_wired_power_wake_pending();
    }
    irq_unlock(key);
}

#ifdef CONFIG_GMK70_RETAINED_SLEEP
void gmk_wired_power_peer_wake(void) {
    /* An incoming A10 edge is already the peer waking us. Never echo it or an
     * RTC-only checkpoint. Restore UART TX AF7 after the bounded wake pulse. */
    if (!gmk_split_available() || (sys_read32(EXTI_PENDING_ADDRESS) & BIT(10))) return;
    struct gmk_wired_pin tx = {0, 9};
    port_of(tx)->bsrr       = BIT(25);
    pin_mode(tx, 1, 0);
    k_busy_wait(100);
    port_of(tx)->bsrr = BIT(9);
    pin_mode(tx, 2, 0);
}
#endif
void gmk_wired_power_resume(void) {
    if (!initialized || k_is_in_isr()) return;
    k_mutex_lock(&wired_lock, K_FOREVER);
    unsigned key = irq_lock();
    if (!power_prepared && !power_paused) goto out;
    if (power_armed & POWER_ARM_A10) gpio_pin_interrupt_configure(gpio_ports[0], 10, GPIO_INT_DISABLE);
    if (power_armed & POWER_ARM_A7) gpio_pin_interrupt_configure(gpio_ports[0], 7, GPIO_INT_DISABLE);
    if (power_armed & POWER_ARM_C14) gpio_pin_interrupt_configure(gpio_ports[2], 14, GPIO_INT_DISABLE);
    if (power_armed & POWER_ARM_C15) gpio_pin_interrupt_configure(gpio_ports[2], 15, GPIO_INT_DISABLE);
    row_callbacks[0].pin_mask &= ~(BIT(7) | BIT(10));
    row_callbacks[2].pin_mask &= ~(BIT(14) | BIT(15));
    encoder_callback.pin_mask = left ? BIT(6) | BIT(7) : 0;
    if (power_critical) {
        for (unsigned r = 0; r < 5; ++r)
            pin_mode(row_pins[r], 0, 1);
        for (unsigned c = 0; c < 9; ++c)
            pin_mode(col_pins[c], 0, 1);
        atomic_set(&scan_pending, 1);
        power_critical = false;
    }
#ifdef CONFIG_GMK70_RETAINED_SLEEP
    if (power_paused) {
        /* Full suspend advances uptime. Rebase a coordinated lease before
         * unpausing or the first waking tap can disable its own retained queue. */
        event_rx_at = last_rx_seen = k_uptime_get_32();
        if (peer.connected) peer.seen = event_rx_at;
        if (event_mode) event_fast = true;
    }
#endif
    power_prepared = power_paused = false;
    if (power_armed & POWER_RESTORE_ENCODER) {
        encoder_irq_ready = power_arm(1, 6) && power_arm(1, 7);
        /* Resynchronize native EC11 state, without device_init on an already
         * initialized device, before accepting fresh post-abort detents. */
        (void)gmk_native_encoder_sample();
        gmk_encoder_clear(&encoder);
    } else if (power_armed & POWER_ARM_B6) {
        gpio_pin_interrupt_configure(gpio_ports[1], 6, GPIO_INT_DISABLE);
    }
    power_armed = 0;
#ifdef CONFIG_GMK70_EVENT_IDLE
    gmk_scan_notify();
    gmk_service_notify();
#endif
out:
    irq_unlock(key);
    k_mutex_unlock(&wired_lock);
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
/* Preserve the native updater entry for ELF admission checks under LTO. */
__attribute__((noipa)) bool gmk_wired_update(const uint8_t request[32], uint8_t response[32]) {
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
#ifdef CONFIG_GMK70_EVENT_IDLE
    atomic_set(&master_receiving, 0);
#endif
    k_mutex_unlock(&wired_lock);
    return ok;
}
bool gmk_wired_split_command(const uint8_t request[32], uint8_t response[32]) {
    memset(response, 0, 32);
    if (!gmk_wired_is_left() || !gmk_split_available() || k_is_in_isr()) return false;
    k_mutex_lock(&wired_lock, K_FOREVER);
    bool ok = gmk_protocol_split_command(&master_wire, request, response);
    if (!ok) ++counters.failures;
#ifdef CONFIG_GMK70_EVENT_IDLE
    atomic_set(&master_receiving, 0);
#endif
    k_mutex_unlock(&wired_lock);
    return ok;
}
/* Preserve the physical/native scanner integration check under LTO. */
__attribute__((noipa)) void gmk_wired_scan(uint16_t rows[5]) {
    uint16_t raw[5] = {0};
    if (!initialized) {
        memset(rows, 0, 10);
        return;
    }
    if (power_paused || (power_prepared && power_critical)) {
        k_mutex_lock(&wired_lock, K_FOREVER);
        memcpy(rows, debounce.stable, 10);
        k_mutex_unlock(&wired_lock);
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
#ifdef CONFIG_GMK70_EVENT_IDLE
    if (!left) {
        uint32_t now = k_uptime_get_32();
        if (event_link.enabled && (uint32_t)(now - event_rx_at) >= GMK_EVENT_LEASE_MS) event_legacy();
        gmk_event_sample(&event_link, rows, scan_idle);
        if (!power_prepared && !power_paused && event_armed && gmk_event_notice_due(&event_link, now)) {
            uint8_t notice = GMK_EVENT_NOTICE;
            deadline       = k_uptime_get() + GMK_WIRE_TIMEOUT_MS;
            if (write_uart(NULL, &notice, 1)) gmk_event_notified(&event_link, now);
        }
    }
#endif
    k_mutex_unlock(&wired_lock);
}
void gmk_wired_poll(void) {
    if (!gmk_wired_is_left() || power_prepared || power_paused) return;
    uint16_t rows[5];
    k_mutex_lock(&wired_lock, K_FOREVER);
    if (power_prepared || power_paused) {
        k_mutex_unlock(&wired_lock);
        return;
    }
    if (gmk_split_available()) {
#ifdef CONFIG_GMK70_EVENT_IDLE
        if (!event_probed) {
            /* Transaction zero disables the peripheral's event queue. Discover
             * capability first, including after this master reboots/wakes. */
            int capability = negotiate_events(false);
            if (!capability) {
                atomic_set(&master_receiving, 0);
                k_mutex_unlock(&wired_lock);
                return;
            }
            event_probed = true;
            event_mode   = capability > 0;
        }
        uint32_t now    = k_uptime_get_32();
        bool     notice = atomic_clear(&event_notice) != 0;
        if (event_mode && !event_fast && !notice && (uint32_t)(now - event_poll_at) < GMK_EVENT_HEARTBEAT_MS) {
            gmk_lighting_wire_poll(&master_wire);
            atomic_set(&master_receiving, 0);
            k_mutex_unlock(&wired_lock);
            return;
        }
        if (event_mode) {
            uint8_t  reply[GMK_EVENT_FRAME], flags;
            uint16_t sequence;
            ++counters.polls;
            bool ok       = gmk_protocol_event_poll(&master_wire, event_ack, event_tag, reply) && gmk_event_decode(reply, &sequence, &flags, rows) && reply[14] == event_tag;
            event_poll_at = k_uptime_get_32();
            if (ok) {
                event_failures = 0;
                if (flags & GMK_EVENT_DATA) event_ack = sequence;
                gmk_peer_set(&peer, rows, event_poll_at);
                bool held = false;
                for (unsigned r = 0; r < 5; ++r)
                    held |= rows[r] != 0;
                peer.quiet_timeout_ms = held ? 0 : GMK_EVENT_LEASE_MS;
                event_fast            = (flags & GMK_EVENT_DATA) || !(flags & GMK_EVENT_QUIET) || held;
                gmk_lighting_wire_poll(&master_wire);
                atomic_set(&master_receiving, 0);
                k_mutex_unlock(&wired_lock);
                return;
            }
            ++counters.failures;
            /* An invalid/lost snapshot is never acknowledged. Keep the same
             * queue/session for bounded retries before probing a rebooted peer. */
            if (++event_failures < 3) {
                event_fast = true;
                atomic_set(&master_receiving, 0);
                k_mutex_unlock(&wired_lock);
                return;
            }
            event_failures = 0;
            int recovery   = negotiate_events(true);
            if (recovery >= 0) {
                /* Preserve a capable peer's queue/session through lost replies.
                 * An ambiguous probe never admits a destructive legacy read. */
                event_fast = true;
                atomic_set(&master_receiving, 0);
                k_mutex_unlock(&wired_lock);
                return;
            }
            event_mode = event_probed = false;
            peer.quiet_timeout_ms     = 0;
        }
#endif
        ++counters.polls;
        if (gmk_protocol_poll(&master_wire, rows)) {
            gmk_peer_set(&peer, rows, k_uptime_get_32());
            gmk_lighting_wire_poll(&master_wire);
        } else
            ++counters.failures;
    } else
        peer.connected = false;
#ifdef CONFIG_GMK70_EVENT_IDLE
    atomic_set(&master_receiving, 0);
#endif
    k_mutex_unlock(&wired_lock);
}
#ifdef CONFIG_GMK70_EVENT_IDLE
uint32_t gmk_wired_scan_delay(void) {
    if (!initialized) return 1;
    if (power_paused) return UINT32_MAX;
    if (!scan_idle || !scan_irq_ready || atomic_get(&scan_pending) || (left && ((encoder_ready && !encoder_irq_ready) || encoder.count))) return 1;
    uint32_t now = k_uptime_get_32(), delay = UINT32_MAX;
    if (left && gmk_split_available() && !power_prepared) {
        delay = !event_mode || event_fast || atomic_get(&event_notice) ? 1 : remaining_ms(now, event_poll_at, GMK_EVENT_HEARTBEAT_MS);
    } else if (!left && event_link.enabled && !power_prepared) {
        delay = remaining_ms(now, event_rx_at, GMK_EVENT_LEASE_MS);
        if (event_armed && event_link.count) {
            uint32_t retry = event_link.notified ? remaining_ms(now, event_link.notice_at, 20) : 0;
            if (retry < delay) delay = retry;
        }
    }
    return delay;
}
#endif
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
    uint32_t timeout = GMK_PEER_TIMEOUT_MS;
#ifdef CONFIG_GMK70_EVENT_IDLE
    if (!left && event_link.enabled && event_armed) timeout = GMK_EVENT_LEASE_MS;
#endif
    stats->peer = left ? gmk_peer_rows(&peer, stats->uptime_ms, rows) : (have_last_rx && (uint32_t)(stats->uptime_ms - last_rx_seen) < timeout);
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
#ifdef CONFIG_GMK70_EVENT_IDLE
            k_sleep(K_FOREVER); /* PB9 role is fixed at startup. */
#else
            k_sleep(K_MSEC(20));
#endif
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
        gmk_service_notify();
        gmk_scan_notify();
        have_first = false;
        k_mutex_unlock(&wired_lock);
    }
}
K_THREAD_DEFINE(gmk_serial_thread, 1536, serial_thread, NULL, NULL, NULL, 4, 0, 0);
static int wired_init(void) {
    protocol.lighting = gmk_lighting_receive;
#ifdef CONFIG_GMK70_EVENT_IDLE
    protocol.event_control = event_control;
    protocol.event_read    = event_read;
    protocol.legacy_read   = event_legacy;
#endif
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
#ifdef CONFIG_GMK70_EVENT_IDLE
    gpio_init_callback(&mode_callback, mode_wake, BIT(14) | BIT(15));
    if (!gpio_add_callback(gpio_ports[2], &mode_callback)) mode_irq_arm();
#endif
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
