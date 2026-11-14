/* Copyright (C) 2021 Westberry Technology (ChangZhou) Corp., Ltd
 * Copyright 2026 GMK70 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 * UART register programming derives from Westberry's Apache-2.0 UARTv1
 * hal_serial_lld.c and pinned WB32FQ95xx CMSIS definitions. Protocol and board
 * pin facts derive from maintained GPL-2.0-or-later qmk-common module.c/config.h.
 * Radio frames share this sole UART3 owner; the external module is unchanged.
 */
#include "battery.h"
#include "wired.h"
#ifdef CONFIG_GMK70_RADIO
#    include "radio.h"
#endif
#include <soc.h>
#include <string.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include "../runtime/idle.h"
#ifdef CONFIG_GMK70_EVENT_IDLE
#    include <zephyr/sys/atomic.h>
K_MSGQ_DEFINE(radio_rx, 1, 128, 1);
static atomic_t radio_rx_fault;
static void     radio_uart_isr(const void *unused) {
    ARG_UNUSED(unused);
    for (unsigned count = 0; count < 64; ++count) {
        uint32_t status = sys_read32(0x40008414U);
        if (status & 0x9eU) {
            atomic_set(&radio_rx_fault, 1);
            sys_write32(3, 0x40008408U); /* Clear damaged RX, preserve TX. */
            break;
        }
        if (!(status & 1U)) break;
        uint8_t byte = (uint8_t)sys_read32(0x40008400U);
        if (k_msgq_put(&radio_rx, &byte, K_NO_WAIT)) atomic_set(&radio_rx_fault, 1);
    }
    /* Parsing, ACK attribution and all UART writes remain worker-owned. */
    gmk_service_notify();
}
#endif

#define BATTERY_UART_BASE 0x40008400U
#define BATTERY_QUERY_MS 3000U
#define BATTERY_TIMEOUT_MS 10U
#define BATTERY_FRESH_MS 10000U

struct battery_parser {
    uint32_t last_byte_ms;
    uint16_t count, length;
    uint8_t  command, value, sum;
    bool     discard;
};
enum battery_transmission { BATTERY_TX_NONE, BATTERY_TX_QUERY, BATTERY_TX_ACK, BATTERY_TX_RADIO };
struct battery_state {
    /* Private worker state: short offsets keep Cortex-M3 byte loads compact. */
    struct battery_parser     parser;
    uint8_t                   tx_offset, acknowledgements;
    bool                      initialized, have_cycle, waiting_ack, retry_used, query_pending, quiesced, demand;
    enum battery_transmission transmitting;
    struct gmk_battery_status status;
    uint32_t                  sample_ms, cycle_ms, ack_ms, tx_ms;
#ifdef CONFIG_GMK70_RADIO
    struct gmk_radio_frame radio_frame;
#endif
};
static struct battery_state state;
static struct {
    struct gmk_battery_status status;
    uint32_t                  sample_ms;
    bool                      requested;
} cache;
static struct k_spinlock cache_lock;

static void pin_configure(uintptr_t port, unsigned pin, uint32_t mode, uint32_t pull) {
    unsigned key = irq_lock();
    sys_write32((uint16_t)~(1U << pin), port + 0x30);
    sys_write32(0, port + 4);
    sys_write32(pull << (2 * pin), port + 0x0c);
    sys_write32(mode << (2 * pin), port);
    if (mode == 2) {
        sys_write32(3U << (2 * pin), port + 8);
        sys_write32(1U << (2 * pin), port + 0x2c);
        sys_write32(7U << ((pin - 8) * 4), port + 0x24);
    }
    irq_unlock(key);
}
static void publish(void) {
    k_spinlock_key_t key = k_spin_lock(&cache_lock);
    cache.status         = state.status;
    cache.sample_ms      = state.sample_ms;
    k_spin_unlock(&cache_lock, key);
}
void gmk_battery_get(struct gmk_battery_status *out) {
    k_spinlock_key_t key = k_spin_lock(&cache_lock);
    *out                 = cache.status;
    uint32_t sample_ms   = cache.sample_ms;
    k_spin_unlock(&cache_lock, key);
    out->age_ms = out->valid || out->replies ? k_uptime_get_32() - sample_ms : UINT32_MAX;
    out->valid  = out->valid && out->age_ms <= BATTERY_FRESH_MS;
}
void gmk_battery_request(void) {
    k_spinlock_key_t key = k_spin_lock(&cache_lock);
    cache.requested      = true;
    k_spin_unlock(&cache_lock, key);
    gmk_service_notify();
}
bool gmk_battery_idle(void) {
    if (!state.initialized || !gmk_wired_is_left()) return true;
    if (state.transmitting != BATTERY_TX_NONE || state.waiting_ack || state.acknowledgements || state.parser.count) return false;
#ifdef CONFIG_GMK70_EVENT_IDLE
    if (k_msgq_num_used_get(&radio_rx) || atomic_get(&radio_rx_fault)) return false;
#endif
    /* LSR.TEMT includes the physical shift register; FIFO admission alone does
     * not establish that the last byte has left the module's UART wire. */
    uint32_t status = sys_read32(BATTERY_UART_BASE + 0x14);
    return (status & 0x40U) != 0 && (status & 0x9fU) == 0;
}
bool gmk_battery_power_quiesce(bool enabled) {
    if (enabled && !gmk_battery_idle()) return false;
    state.quiesced = enabled;
#ifdef CONFIG_GMK70_EVENT_IDLE
    if (state.initialized && gmk_wired_is_left()) sys_write32(enabled ? 0 : 5, BATTERY_UART_BASE + 4);
#endif
    if (enabled) state.query_pending = false;
    return true;
}

static bool recognized(uint8_t byte) {
    return byte == 0x5a || byte == 0x5b || byte == 0x5c || byte == 0x5d || byte == 0x60 || byte == 0x61 || byte == 0xaf;
}
static void begin_frame(uint8_t byte, uint32_t now) {
    state.parser = (struct battery_parser){0};
    if (!recognized(byte)) return;
    state.parser.command      = byte;
    state.parser.sum          = byte;
    state.parser.count        = 1;
    state.parser.length       = 3;
    state.parser.last_byte_ms = now;
}
static void accept_frame(uint32_t now) {
    struct battery_parser *parser = &state.parser;
    if (parser->command == 0x61) {
#ifdef CONFIG_GMK70_RADIO
        if (!state.waiting_ack) {
            gmk_radio_ack();
            return;
        }
#endif
        state.waiting_ack = false;
        if (state.retry_used) state.query_pending = false;
        return;
    }
    /* A small count bounds outstanding ACK work during unsolicited traffic. */
    if (state.acknowledgements < 16)
        ++state.acknowledgements;
    else
        ++state.status.errors;
#ifdef CONFIG_GMK70_RADIO
    if (parser->command != 0xaf) gmk_radio_notification(parser->command, parser->value);
#endif
    if (parser->command != 0x5c) return;
    if (parser->value > 100) {
        ++state.status.errors;
        return;
    }
    state.status.percentage = parser->value;
    state.status.valid      = true;
    state.sample_ms         = now;
    ++state.status.replies;
}
static void receive_byte(uint8_t byte, uint32_t now) {
    struct battery_parser *parser = &state.parser;
    if (!parser->count) {
        begin_frame(byte, now);
        return;
    }
    parser->last_byte_ms = now;
    if (parser->count == 1) {
        if (parser->command == 0xaf && byte != 0x60) {
            ++state.status.errors;
            begin_frame(byte, now);
            return;
        }
        parser->value = byte;
        parser->sum   = (uint8_t)(parser->sum + byte);
        ++parser->count;
        return;
    }
    if (parser->command == 0xaf && parser->count == 2) {
        parser->length  = (uint16_t)byte + 4;
        parser->discard = byte > 32;
        if (parser->discard) ++state.status.errors;
    }
    if (parser->count + 1 < parser->length) {
        parser->sum = (uint8_t)(parser->sum + byte);
        ++parser->count;
        return;
    }
    bool valid = parser->command == 0x61 ? parser->value == 0x0d && byte == 0x0a : parser->sum == byte;
    if (!valid && !parser->discard) ++state.status.errors;
    if (valid && !parser->discard) accept_frame(now);
    parser->count = 0;
}
static bool quarantine_raw_frame(void) {
    struct battery_parser *parser = &state.parser;
    if (!parser->count || (parser->command != 0xaf && !parser->discard)) return false;
    parser->discard = true;
    if (parser->command == 0xaf && parser->count < 3) {
        /* Before the raw length arrives, bound its possible remainder by the
         * largest one-byte length: 255 payload bytes plus four framing bytes.
         * Keep already consumed bytes in count; command zero skips parsing. */
        parser->command = 0;
        parser->length  = 259;
    }
    return true;
}
static void drain_rx(uint32_t now) {
    if (state.parser.count && !state.parser.discard && (uint32_t)(now - state.parser.last_byte_ms) >= BATTERY_TIMEOUT_MS) {
        /* A raw tail may arrive after an empty timeout poll. Retain its byte
         * budget across idle polls rather than treating delayed payload as new
         * frames. A truly truncated raw frame can consume later valid samples
         * until that finite budget is exhausted; unknown is safer than a false
         * percentage. Expired raw frames are never acknowledged or accepted. */
        if (!quarantine_raw_frame()) {
            state.parser.count = 0;
            sys_write32(3, BATTERY_UART_BASE + 8);
        }
        ++state.status.errors;
    }
    for (unsigned count = 0; count < 64; ++count) {
#ifdef CONFIG_GMK70_EVENT_IDLE
        if (atomic_clear(&radio_rx_fault)) {
            if (!quarantine_raw_frame()) state.parser.count = 0;
            k_msgq_purge(&radio_rx);
            ++state.status.errors;
        }
        uint8_t byte;
        if (k_msgq_get(&radio_rx, &byte, K_NO_WAIT)) break;
        receive_byte(byte, now);
#else
        uint32_t status = sys_read32(BATTERY_UART_BASE + 0x14);
        if (status & 0x9e) {
            /* Drop the damaged FIFO, preserving any raw discard budget: the
             * number of bytes lost to the hardware fault is unknown. This can
             * over-discard future frames, but cannot expose a late raw tail. */
            sys_write32(3, BATTERY_UART_BASE + 8);
            if (!quarantine_raw_frame()) state.parser.count = 0;
            ++state.status.errors;
            break;
        }
        if (!(status & 1)) break;
        receive_byte((uint8_t)sys_read32(BATTERY_UART_BASE), now);
#endif
    }
}
static bool automatic_queries(void) {
#ifdef CONFIG_GMK70_RADIO
    /* Shortcuts can select wireless with the slider in USB, and vice versa.
     * Keep physical wired-mode checks reserved for updater/storage admission. */
    return gmk_radio_selected();
#else
    return !gmk_wired_mode();
#endif
}
static void schedule_query(uint32_t now) {
    bool automatic = automatic_queries();
    bool allowed   = automatic || state.demand;
    /* Cancel queued periodic work on wired entry, without interrupting a
     * partially transmitted frame or stealing its outstanding ACK. */
    if (!allowed) state.query_pending = false;
    if (state.waiting_ack && (uint32_t)(now - state.ack_ms) >= BATTERY_TIMEOUT_MS) {
        state.waiting_ack = false;
        if (!state.retry_used && allowed) {
            state.retry_used    = true;
            state.query_pending = true;
        } else {
            ++state.status.errors;
        }
    }
    if (!state.query_pending && !state.waiting_ack && state.transmitting != BATTERY_TX_QUERY) {
        k_spinlock_key_t key       = k_spin_lock(&cache_lock);
        bool             requested = cache.requested;
        cache.requested            = false;
        k_spin_unlock(&cache_lock, key);
        if (!requested && (!automatic || (state.have_cycle && (uint32_t)(now - state.cycle_ms) < BATTERY_QUERY_MS))) return;
        state.demand        = requested;
        state.have_cycle    = true;
        state.cycle_ms      = now;
        state.retry_used    = false;
        state.query_pending = true;
    }
}
static void transmit(uint32_t now) {
    if (state.transmitting != BATTERY_TX_NONE && (uint32_t)(now - state.tx_ms) >= BATTERY_TIMEOUT_MS) {
#ifdef CONFIG_GMK70_RADIO
        if (state.transmitting == BATTERY_TX_RADIO) gmk_radio_sent(now, false);
#endif
        state.transmitting = BATTERY_TX_NONE;
        state.waiting_ack  = false;
        ++state.status.errors;
    }
    if (state.transmitting == BATTERY_TX_NONE) {
        if (state.acknowledgements) {
            --state.acknowledgements;
            state.transmitting = BATTERY_TX_ACK;
        } else if (state.waiting_ack) {
            return;
#ifdef CONFIG_GMK70_RADIO
        } else if (state.query_pending && gmk_radio_battery_ready(now)) {
            /* A stream of HID reports must not starve the three-second query.
             * Controls, release barriers and an A1/A2 pair still finish first. */
            state.query_pending = false;
            state.transmitting  = BATTERY_TX_QUERY;
        } else if (gmk_radio_next(now, &state.radio_frame)) {
            if (!state.radio_frame.length || state.radio_frame.length > sizeof(state.radio_frame.bytes)) {
                gmk_radio_sent(now, false);
                ++state.status.errors;
                return;
            }
            state.transmitting = BATTERY_TX_RADIO;
#else
        } else if (state.query_pending) {
            state.query_pending = false;
            state.transmitting  = BATTERY_TX_QUERY;
#endif
        } else {
            return;
        }
        state.tx_ms     = now;
        state.tx_offset = 0;
    }
    static const uint8_t query[3] = {0xa6, 0x53, 0xf9};
    static const uint8_t ack[3]   = {0x61, 0x0d, 0x0a};
    const uint8_t       *message  = state.transmitting == BATTERY_TX_QUERY ? query : ack;
    uint8_t              length   = 3;
#ifdef CONFIG_GMK70_RADIO
    if (state.transmitting == BATTERY_TX_RADIO) {
        message = state.radio_frame.bytes;
        length  = state.radio_frame.length;
    }
    /* One immutable frame, at most sixteen FIFO writes per tick. ACK priority
     * applies only when choosing a new frame, never within a partial frame. */
#else
    /* At most three FIFO writes per worker tick, with no wait for TX space. */
#endif
    while (state.tx_offset < length && (sys_read32(BATTERY_UART_BASE + 0x7c) & 2)) {
        sys_write32(message[state.tx_offset++], BATTERY_UART_BASE);
    }
    if (state.tx_offset != length) return;
    if (state.transmitting == BATTERY_TX_QUERY) {
        ++state.status.queries;
        state.waiting_ack = true;
        state.ack_ms      = now;
    }
#ifdef CONFIG_GMK70_RADIO
    if (state.transmitting == BATTERY_TX_RADIO) gmk_radio_sent(now, true);
#endif
    state.transmitting = BATTERY_TX_NONE;
}
void gmk_battery_poll(void) {
    if (!state.initialized || !gmk_wired_is_left() || state.quiesced) return;
    uint32_t now       = k_uptime_get_32();
    uint32_t pins      = sys_read32(WB32_GPIOA_BASE + 0x10);
    state.status.cable = (pins & (1U << 7)) != 0;
    state.status.full  = state.status.cable && (pins & (1U << 15)) != 0;
#ifdef CONFIG_GMK70_RADIO
    gmk_radio_charger_state(state.status.cable, state.status.full);
#endif
    drain_rx(now);
    if (state.status.valid && (uint32_t)(now - state.sample_ms) > BATTERY_FRESH_MS) state.status.valid = false;
    schedule_query(now);
    transmit(now);
    publish();
}
#ifdef CONFIG_GMK70_EVENT_IDLE
uint32_t gmk_battery_delay(void) {
    if (!state.initialized || !gmk_wired_is_left() || state.quiesced) return UINT32_MAX;
    if (k_msgq_num_used_get(&radio_rx) || atomic_get(&radio_rx_fault) || state.transmitting != BATTERY_TX_NONE || state.acknowledgements || state.query_pending) return 1;
    uint32_t now = k_uptime_get_32(), delay = 1000;
    if (state.waiting_ack) {
        uint32_t elapsed = now - state.ack_ms;
        delay            = elapsed >= BATTERY_TIMEOUT_MS ? 1 : BATTERY_TIMEOUT_MS - elapsed;
    }
    if (state.parser.count && !state.parser.discard) {
        uint32_t elapsed = now - state.parser.last_byte_ms;
        uint32_t next    = elapsed >= BATTERY_TIMEOUT_MS ? 1 : BATTERY_TIMEOUT_MS - elapsed;
        if (next < delay) delay = next;
    }
    if (automatic_queries()) {
        uint32_t elapsed = now - state.cycle_ms;
        uint32_t next    = !state.have_cycle || elapsed >= BATTERY_QUERY_MS ? 1 : BATTERY_QUERY_MS - elapsed;
        if (next < delay) delay = next;
    }
    k_spinlock_key_t key = k_spin_lock(&cache_lock);
    if (cache.requested) delay = 1;
    k_spin_unlock(&cache_lock, key);
    /* A7 cable/charge pins share EXTI lines with encoder/mode inputs. This
     * bounded fallback samples them without stealing an input wake source. */
    return delay;
}
#endif
static int battery_init(void) {
    memset(&state, 0, sizeof(state));
    cache.requested = false;
    publish();
    if (!gmk_wired_is_left()) return 0;
    sys_set_bits(WB32_RCC_BASE + 0x48, (1U << 15) | (1U << 5) | (1U << 7));
    pin_configure(WB32_GPIOA_BASE, 7, 0, 0);
    pin_configure(WB32_GPIOA_BASE, 15, 0, 1);
    pin_configure(WB32_GPIOC_BASE, 10, 2, 0);
    pin_configure(WB32_GPIOC_BASE, 11, 2, 0);
    sys_set_bits(WB32_RCC_BASE + 0x4c, 1U << 3);
    sys_set_bits(WB32_RCC_BASE + 0x94, 1U << 3);
    sys_clear_bits(WB32_RCC_BASE + 0x94, 1U << 3);
    /* PCLK2 = 96 MHz, UART3 PC10/PC11 AF7, 115200 8N1, no flow control.
     * Divisor is 833 = 52*16 + 1. Interrupts remain disabled. */
    sys_write32(0, BATTERY_UART_BASE + 4);
    sys_write32(0, BATTERY_UART_BASE + 0x10);
    sys_write32(0x80, BATTERY_UART_BASE + 0x0c);
    sys_write32(1, BATTERY_UART_BASE + 0xc0);
    sys_write32(52, BATTERY_UART_BASE);
    sys_write32(0, BATTERY_UART_BASE + 4);
    sys_write32(3, BATTERY_UART_BASE + 0x0c);
    sys_write32(7, BATTERY_UART_BASE + 8);
    sys_write32(0, BATTERY_UART_BASE + 0x9c);
    sys_write32(1, BATTERY_UART_BASE + 0x98);
    state.initialized = true;
#ifdef CONFIG_GMK70_EVENT_IDLE
    IRQ_CONNECT(32, 1, radio_uart_isr, NULL, 0);
    irq_enable(32);
    sys_write32(5, BATTERY_UART_BASE + 4); /* RX data and line status, no TX IRQ. */
#endif
    return 0;
}
SYS_INIT(battery_init, POST_KERNEL, 86);
