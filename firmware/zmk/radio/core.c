/* Copyright 2026 GMK70 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Vendor framing/report facts derive from maintained qmk-common wireless
 * module.c/module.h (Copyright 2024 Su (@isuua)). No pairing data is erased.
 */
#include "core.h"
#include <errno.h>
#include <string.h>

enum {
    ACK_DRAIN_MS = 20,
#if defined(CONFIG_ZMK_POINTING) && CONFIG_ZMK_POINTING
    RELEASE_ALL = 15
#else
    RELEASE_ALL = 7
#endif
};

static bool wireless(uint8_t channel) {
    return channel > GMK_HOST_USB && channel <= GMK_HOST_DONGLE;
}
static bool usb_quiet(const struct gmk_radio_core *radio) {
    return radio->last_control == GMK_RADIO_TX_SELECT && radio->selected == GMK_HOST_USB && radio->active == GMK_HOST_USB && radio->accept_status && !radio->faulted;
}
static bool acknowledged_control(uint8_t kind) {
    return (kind >= GMK_RADIO_TX_SELECT && kind <= GMK_RADIO_TX_PAIR) || (kind >= GMK_RADIO_TX_SLEEP_BT && kind <= GMK_RADIO_TX_CHARGER);
}
static void discard(struct gmk_radio_core *radio) {
    radio->head = radio->count = 0;
    radio->overflow_pending    = false;
}
static void retire(struct gmk_radio_core *radio, uint8_t kind) {
    /* One flash lookup keeps the six retirement cases inside the board's
     * application budget without reserving mutable state. Other kinds have
     * no one-shot bit to consume. */
    static const uint8_t masks[GMK_RADIO_TX_SLEEP_DONGLE - GMK_RADIO_TX_RELEASE_KEYBOARD + 1] = {
        [GMK_RADIO_TX_RELEASE_KEYBOARD - GMK_RADIO_TX_RELEASE_KEYBOARD] = 1, [GMK_RADIO_TX_RELEASE_CONSUMER - GMK_RADIO_TX_RELEASE_KEYBOARD] = 2, [GMK_RADIO_TX_RELEASE_NKRO - GMK_RADIO_TX_RELEASE_KEYBOARD] = 4, [GMK_RADIO_TX_RELEASE_MOUSE - GMK_RADIO_TX_RELEASE_KEYBOARD] = 8, [GMK_RADIO_TX_SLEEP_BT - GMK_RADIO_TX_RELEASE_KEYBOARD] = 1, [GMK_RADIO_TX_SLEEP_DONGLE - GMK_RADIO_TX_RELEASE_KEYBOARD] = 2,
    };
    if (kind < GMK_RADIO_TX_RELEASE_KEYBOARD || kind > GMK_RADIO_TX_SLEEP_DONGLE) return;
    uint8_t *pending = kind < GMK_RADIO_TX_SLEEP_BT ? &radio->release : &radio->sleep_pending;
    *pending &= (uint8_t)~masks[kind - GMK_RADIO_TX_RELEASE_KEYBOARD];
}
static void drain(struct gmk_radio_core *radio, uint8_t kind, uint32_t now) {
    /* ACKs carry no command ID. Retire the old owner before another control
     * can start, and absorb late ACKs for two ordinary ACK deadlines. This is
     * a finite quarantine, not attribution of arbitrarily delayed replies. */
    retire(radio, kind);
    radio->waiting = radio->retransmit = radio->obsolete = false;
    radio->draining                                      = true;
    radio->sent_at                                       = now;
}
static void abandon(struct gmk_radio_core *radio, uint8_t kind, uint32_t now) {
    /* Hard failures invalidate report ownership. The ACK-timeout caller may
     * retain a fully sent SELECT's status context. Clear ownership once and
     * attempt bounded zero reports; repeatedly recreating that work on a
     * failed UART would itself prevent battery protection. */
    if (!radio->faulted) {
        if (wireless(radio->active) || wireless(radio->selected)) radio->release |= RELEASE_ALL;
        radio->clear_requested = true;
    }
    radio->faulted = true;
    radio->waiting = radio->pending_select = radio->pending_pair = radio->accept_status = radio->retransmit = false;
    radio->connection                                                                                       = GMK_RADIO_UNKNOWN;
    radio->retries                                                                                          = 0;
    discard(radio);
    retire(radio, kind);
    if (kind == GMK_RADIO_TX_CHARGER) {
        /* Abandon this snapshot until its next heartbeat. A changed physical
         * charging state still takes precedence over the failed old snapshot. */
        radio->charge_sent = radio->tx_channel;
        radio->charge_at   = now;
    }
    ++radio->errors;
}
void gmk_radio_core_init(struct gmk_radio_core *radio) {
    *radio = (struct gmk_radio_core){.last_bt = GMK_HOST_BT1, .pending_select = true};
}
int gmk_radio_core_select(struct gmk_radio_core *radio, uint8_t channel, bool pair) {
    if (channel > GMK_HOST_DONGLE || (pair && !wireless(channel))) return -EINVAL;
    bool outstanding = radio->waiting || radio->inflight;
    if (!pair && channel == radio->selected && radio->accept_status && !radio->faulted && !radio->pending_select && !radio->pending_pair && !outstanding && radio->connection != GMK_RADIO_PAIRING) return 0;
    radio->obsolete |= outstanding;
    discard(radio);
    if (wireless(radio->active)) radio->release |= RELEASE_ALL;
    radio->selected = channel;
    if (channel >= GMK_HOST_BT1 && channel <= GMK_HOST_BT5) radio->last_bt = channel;
    radio->pending_select = channel != radio->active || !radio->accept_status || radio->faulted || radio->waiting || acknowledged_control(radio->inflight) || radio->connection == GMK_RADIO_PAIRING;
    radio->faulted        = false;
    radio->pending_pair   = pair;
    /* A same-channel PAIR remains cancellable until its frame is emitted.
     * pending_pair inhibits reports without losing a live connection when
     * release or Studio reassignment cancels the queued pairing request. */
    if (radio->pending_select) radio->connection = GMK_RADIO_UNKNOWN;
    radio->accept_status = !radio->pending_select;
    return 0;
}
static int enqueue(struct gmk_radio_core *radio, uint8_t kind, const uint8_t bytes[8], const uint8_t overflow[14]) {
    if (radio->faulted || !wireless(radio->selected) || radio->active != radio->selected || radio->connection != GMK_RADIO_CONNECTED || radio->pending_pair) return -ENOTCONN;
    bool full = radio->count == 8;
    if (full) {
        discard(radio);
        radio->release |= RELEASE_ALL;
        ++radio->errors;
    }
    struct gmk_radio_report *report = &radio->queue[(radio->head + radio->count) % 8];
    memcpy(report->bytes, bytes, sizeof(report->bytes));
    if (overflow) memcpy(report->overflow, overflow, sizeof(report->overflow));
    report->kind = kind;
    ++radio->count;
    return full ? -ENOBUFS : 0;
}
int gmk_radio_core_report(struct gmk_radio_core *radio, bool consumer, const uint8_t bytes[8]) {
    return enqueue(radio, consumer ? GMK_RADIO_TX_CONSUMER : GMK_RADIO_TX_KEYBOARD, bytes, NULL);
}
int gmk_radio_core_mouse(struct gmk_radio_core *radio, const uint8_t bytes[5]) {
    const uint8_t report[8] = {bytes[0], bytes[1], bytes[2], bytes[3], bytes[4]};
    return enqueue(radio, GMK_RADIO_TX_MOUSE, report, NULL);
}
static void release_mask(struct gmk_radio_core *radio, uint8_t mask) {
    if (!wireless(radio->active) && !wireless(radio->selected)) return;
    /* A reserved barrier survives a full FIFO and discards obsolete presses. */
    if (radio->overflow_pending) radio->release |= 5;
    discard(radio);
    radio->release |= mask;
}
void gmk_radio_core_release(struct gmk_radio_core *radio, bool consumer) {
    release_mask(radio, consumer ? 2 : 5);
}
void gmk_radio_core_release_mouse(struct gmk_radio_core *radio) {
    release_mask(radio, 8);
}
void gmk_radio_core_enable_sleep(struct gmk_radio_core *radio) {
    radio->sleep_pending |= 3;
}
void gmk_radio_core_charger(struct gmk_radio_core *radio, bool cable, bool full) {
    radio->charge_mode = cable ? (full ? 0x66 : 0x64) : 0x65;
}
bool gmk_radio_core_control_busy(const struct gmk_radio_core *radio) {
    return radio->waiting || radio->obsolete || radio->draining || radio->pending_select || radio->pending_pair || radio->sleep_pending || radio->charge_mode != radio->charge_sent || usb_quiet(radio) || acknowledged_control(radio->inflight);
}
bool gmk_radio_core_idle(const struct gmk_radio_core *radio) {
    /* A passive diagnostic fault owns no UART/control lease. The battery
     * owner separately verifies physical TX/RX and parser quiescence. */
    return radio->connection != GMK_RADIO_PAIRING && !radio->count && !radio->release && !radio->inflight && !radio->clear_requested && !gmk_radio_core_control_busy(radio);
}
int gmk_radio_core_nkro(struct gmk_radio_core *radio, const uint8_t keyboard[8], const uint8_t overflow[14]) {
    return enqueue(radio, GMK_RADIO_TX_NKRO, keyboard, overflow);
}
void gmk_radio_core_notification(struct gmk_radio_core *radio, uint8_t command, uint8_t value) {
    if (command != 0x5b || !radio->accept_status || !wireless(radio->active) || radio->active != radio->selected) return;
    uint8_t connection;
    switch (value) {
        case 0x31:
            connection = GMK_RADIO_PAIRING;
            break;
        case 0x32:
            connection = GMK_RADIO_CONNECTED;
            break;
        case 0x33:
            connection = GMK_RADIO_DISCONNECTED;
            break;
        case 0x36:
            connection = GMK_RADIO_REJECTED;
            break;
        default:
            return;
    }
    if (connection == radio->connection) return;
    radio->connection = connection;
    if (connection == GMK_RADIO_CONNECTED) radio->faulted = false;
    discard(radio);
    radio->release |= RELEASE_ALL;
    radio->clear_requested = true;
}
void gmk_radio_core_ack(struct gmk_radio_core *radio) {
    if (!radio->waiting) return;
    retire(radio, radio->last_control);
    radio->waiting = false;
    radio->retries = 0;
}
static bool claim(struct gmk_radio_core *radio, struct gmk_radio_frame *frame, enum gmk_radio_tx_kind kind, uint8_t length) {
    frame->length = length;
    uint8_t sum   = 0;
    for (uint8_t i = 0; i < length - 1; ++i)
        sum = (uint8_t)(sum + frame->bytes[i]);
    frame->bytes[length - 1] = sum;
    radio->inflight          = (uint8_t)kind;
    return true;
}
static bool control(struct gmk_radio_core *radio, struct gmk_radio_frame *frame, enum gmk_radio_tx_kind kind, uint8_t channel) {
    radio->tx_channel = channel;
    frame->bytes[0]   = 0xa6;
    frame->bytes[1]   = kind == GMK_RADIO_TX_CHARGER ? channel : kind == GMK_RADIO_TX_PAIR ? 0x51 : kind == GMK_RADIO_TX_SLEEP_BT ? 0x55 : kind == GMK_RADIO_TX_SLEEP_DONGLE ? 0x57 : channel == GMK_HOST_USB ? 0x11 : channel == GMK_HOST_DONGLE ? 0x30 : (uint8_t)(0x30 + channel);
    return claim(radio, frame, kind, 3);
}
bool gmk_radio_core_next(struct gmk_radio_core *radio, uint32_t now, struct gmk_radio_frame *frame) {
    if (radio->inflight) return false;
    if (radio->obsolete || (radio->waiting && radio->last_control == GMK_RADIO_TX_SELECT && radio->accept_status && radio->connection == GMK_RADIO_CONNECTED)) {
        if (!radio->obsolete) ++radio->errors;
        drain(radio, radio->last_control, now);
    }
    if (radio->draining) {
        if ((uint32_t)(now - radio->sent_at) < ACK_DRAIN_MS) return false;
        radio->draining     = false;
        radio->last_control = GMK_RADIO_TX_NONE;
    }
    memset(frame, 0, sizeof(*frame));
    if (radio->waiting) {
        if ((uint32_t)(now - radio->sent_at) < 10) return false;
        if (radio->retries == (radio->last_control == GMK_RADIO_TX_CHARGER ? 1 : 50)) {
            if (radio->last_control == GMK_RADIO_TX_CHARGER) {
                /* Repeat the idempotent state on its next heartbeat. A lost
                 * charger ACK must not disconnect typing or postpone sleep. */
                radio->waiting = false;
                ++radio->errors;
                return false;
            }
            bool recoverable = radio->last_control == GMK_RADIO_TX_SELECT && radio->accept_status && wireless(radio->active);
            abandon(radio, radio->last_control, now);
            if (recoverable) {
                /* The complete SELECT established a status context even when
                 * its separate transport ACK was lost. A later CONNECTED may
                 * recover it; a partial/failed UART frame never gets this. */
                radio->accept_status = true;
                drain(radio, radio->last_control, now);
            }
            return false;
        }
        ++radio->retries;
        radio->waiting    = false;
        radio->retransmit = true;
        return control(radio, frame, (enum gmk_radio_tx_kind)radio->last_control, radio->last_control == GMK_RADIO_TX_CHARGER ? radio->tx_channel : radio->active);
    }
    if (usb_quiet(radio)) {
        /* USB selection stops the stock module's ADC. Let its four-second
         * UART idle timer sleep once; the next command wakes its ten-second
         * ADC sampler. Resume the ordinary cadence after this one interval.
         * ACKs remain with the battery UART owner and do not reset that timer. */
        if ((uint32_t)(now - radio->sent_at) < 5500U) return false;
        radio->last_control = GMK_RADIO_TX_NONE;
    }
    radio->retransmit = false;
    if (radio->release & 1) {
        frame->bytes[0] = 0xa1;
        return claim(radio, frame, GMK_RADIO_TX_RELEASE_KEYBOARD, 10);
    }
    if (radio->release & 4) {
        frame->bytes[0] = 0xa2;
        return claim(radio, frame, GMK_RADIO_TX_RELEASE_NKRO, 16);
    }
    if (radio->release & 2) {
        frame->bytes[0] = 0xa3;
        return claim(radio, frame, GMK_RADIO_TX_RELEASE_CONSUMER, 4);
    }
    if (radio->release & 8) {
        frame->bytes[0] = 0xa8;
        return claim(radio, frame, GMK_RADIO_TX_RELEASE_MOUSE, 7);
    }
    if (radio->pending_select || (!radio->faulted && radio->selected != radio->active)) return control(radio, frame, GMK_RADIO_TX_SELECT, radio->selected);
    if (radio->pending_pair) return control(radio, frame, GMK_RADIO_TX_PAIR, radio->selected);
    if (radio->sleep_pending & 1) return control(radio, frame, GMK_RADIO_TX_SLEEP_BT, radio->active);
    if (radio->sleep_pending & 2) return control(radio, frame, GMK_RADIO_TX_SLEEP_DONGLE, radio->active);
    if (radio->charge_mode && (radio->charge_mode != radio->charge_sent || (uint32_t)(now - radio->charge_at) >= 1000U)) return control(radio, frame, GMK_RADIO_TX_CHARGER, radio->charge_mode);
    if (!radio->count || radio->connection != GMK_RADIO_CONNECTED || radio->active != radio->selected) return false;
    const struct gmk_radio_report *report = &radio->queue[radio->head];
    if (radio->overflow_pending) {
        frame->bytes[0] = 0xa2;
        memcpy(frame->bytes + 1, report->overflow, sizeof(report->overflow));
        radio->overflow_pending = false;
        radio->head             = (uint8_t)((radio->head + 1) % 8);
        --radio->count;
        return claim(radio, frame, GMK_RADIO_TX_NKRO, 16);
    }
    bool consumer   = report->kind == GMK_RADIO_TX_CONSUMER;
    bool mouse      = report->kind == GMK_RADIO_TX_MOUSE;
    frame->bytes[0] = consumer ? 0xa3 : mouse ? 0xa8 : 0xa1;
    memcpy(frame->bytes + 1, report->bytes, consumer ? 2U : mouse ? 5U : 8U);
    if (report->kind == GMK_RADIO_TX_NKRO)
        /* Keep the snapshot at the head until its paired A2 is claimed. A
         * discard cancels that pair and reserves zero A1/A2 release frames. */
        radio->overflow_pending = true;
    else {
        radio->head = (uint8_t)((radio->head + 1) % 8);
        --radio->count;
    }
    return claim(radio, frame, consumer ? GMK_RADIO_TX_CONSUMER : mouse ? GMK_RADIO_TX_MOUSE : GMK_RADIO_TX_KEYBOARD, consumer ? 4 : mouse ? 7 : 10);
}
void gmk_radio_core_sent(struct gmk_radio_core *radio, uint32_t now, bool success) {
    uint8_t kind = radio->inflight;
    if (!kind) return;
    radio->inflight = GMK_RADIO_TX_NONE;
    if (!success) {
        if (radio->obsolete) {
            ++radio->errors;
            drain(radio, kind, now);
        } else
            abandon(radio, kind, now);
        return;
    }
    if (acknowledged_control(kind)) {
        if (kind == GMK_RADIO_TX_CHARGER) {
            radio->charge_sent = radio->tx_channel;
            radio->charge_at   = now;
        }
        if (kind == GMK_RADIO_TX_SELECT) {
            radio->active         = radio->tx_channel;
            radio->pending_select = radio->obsolete || radio->selected != radio->active;
            radio->accept_status  = !radio->pending_select;
            ++radio->selections;
        } else if (kind == GMK_RADIO_TX_PAIR && !radio->obsolete) {
            radio->pending_pair = false;
            radio->connection   = GMK_RADIO_PAIRING;
        }
        if (!radio->retransmit) radio->retries = 0;
        radio->waiting      = true;
        radio->sent_at      = now;
        radio->last_control = kind;
    } else {
        radio->obsolete = false;
        retire(radio, kind);
        ++radio->reports;
    }
}
