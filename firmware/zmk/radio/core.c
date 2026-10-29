/* Copyright 2026 GMK70 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Vendor framing/report facts derive from maintained qmk-common wireless
 * module.c/module.h (Copyright 2024 Su (@isuua)). No pairing data is erased.
 */
#include "core.h"
#include <errno.h>
#include <string.h>

static bool wireless(uint8_t channel) {
    return channel > GMK_HOST_USB && channel <= GMK_HOST_DONGLE;
}
static void discard(struct gmk_radio_core *radio) {
    radio->head = radio->count = 0;
}
void gmk_radio_core_init(struct gmk_radio_core *radio) {
    *radio = (struct gmk_radio_core){.last_bt = GMK_HOST_BT1, .pending_select = true};
}
int gmk_radio_core_select(struct gmk_radio_core *radio, uint8_t channel, bool pair) {
    if (channel > GMK_HOST_DONGLE || (pair && !wireless(channel))) return -EINVAL;
    if (!pair && channel == radio->selected && radio->accept_status && !radio->pending_select) return 0;
    radio->faulted = false;
    discard(radio);
    if (wireless(radio->active)) radio->release |= 3;
    radio->selected = channel;
    if (channel >= GMK_HOST_BT1 && channel <= GMK_HOST_BT5) radio->last_bt = channel;
    radio->pending_select = channel != radio->active || !radio->accept_status;
    radio->pending_pair   = pair;
    /* A same-channel PAIR remains cancellable until its frame is emitted.
     * pending_pair inhibits reports without losing a live connection when
     * release or Studio reassignment cancels the queued pairing request. */
    if (radio->pending_select) radio->connection = GMK_RADIO_UNKNOWN;
    radio->accept_status = !radio->pending_select;
    return 0;
}
int gmk_radio_core_report(struct gmk_radio_core *radio, bool consumer, const uint8_t bytes[8]) {
    if (!wireless(radio->selected) || radio->active != radio->selected || radio->connection != GMK_RADIO_CONNECTED || radio->pending_pair) return -ENOTCONN;
    bool overflow = radio->count == 8;
    if (overflow) {
        discard(radio);
        radio->release |= 3;
        ++radio->errors;
    }
    struct gmk_radio_report *report = &radio->queue[(radio->head + radio->count) % 8];
    memcpy(report->bytes, bytes, sizeof(report->bytes));
    report->consumer = consumer;
    ++radio->count;
    return overflow ? -ENOBUFS : 0;
}
void gmk_radio_core_release(struct gmk_radio_core *radio, bool consumer) {
    if (!wireless(radio->active) && !wireless(radio->selected)) return;
    /* A reserved barrier survives a full FIFO and discards obsolete presses. */
    discard(radio);
    radio->release |= consumer ? 2 : 1;
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
    discard(radio);
    radio->release |= 3;
    radio->clear_requested = true;
}
void gmk_radio_core_ack(struct gmk_radio_core *radio) {
    if (!radio->waiting) return;
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
static bool control(struct gmk_radio_core *radio, struct gmk_radio_frame *frame, bool pair, uint8_t channel) {
    radio->tx_channel = channel;
    frame->bytes[0]   = 0xa6;
    frame->bytes[1]   = pair ? 0x51 : channel == GMK_HOST_USB ? 0x11 : channel == GMK_HOST_DONGLE ? 0x30 : (uint8_t)(0x30 + channel);
    return claim(radio, frame, pair ? GMK_RADIO_TX_PAIR : GMK_RADIO_TX_SELECT, 3);
}
bool gmk_radio_core_next(struct gmk_radio_core *radio, uint32_t now, struct gmk_radio_frame *frame) {
    if (radio->inflight || radio->faulted) return false;
    memset(frame, 0, sizeof(*frame));
    if (radio->waiting) {
        if ((uint32_t)(now - radio->sent_at) < 10) return false;
        if (radio->retries == 50) {
            radio->waiting = radio->pending_select = radio->pending_pair = radio->accept_status = false;
            radio->connection                                                                   = GMK_RADIO_UNKNOWN;
            discard(radio);
            radio->clear_requested = true;
            radio->faulted         = true;
            ++radio->errors;
            return false;
        }
        ++radio->retries;
        radio->waiting    = false;
        radio->retransmit = true;
        return control(radio, frame, radio->last_control == GMK_RADIO_TX_PAIR, radio->active);
    }
    radio->retransmit = false;
    if (radio->release & 1) {
        frame->bytes[0] = 0xa1;
        return claim(radio, frame, GMK_RADIO_TX_RELEASE_KEYBOARD, 10);
    }
    if (radio->release & 2) {
        frame->bytes[0] = 0xa3;
        return claim(radio, frame, GMK_RADIO_TX_RELEASE_CONSUMER, 4);
    }
    if (radio->pending_select || radio->selected != radio->active) return control(radio, frame, false, radio->selected);
    if (radio->pending_pair) return control(radio, frame, true, radio->selected);
    if (!radio->count || radio->connection != GMK_RADIO_CONNECTED || radio->active != radio->selected) return false;
    const struct gmk_radio_report *report   = &radio->queue[radio->head];
    bool                           consumer = report->consumer;
    frame->bytes[0]                         = consumer ? 0xa3 : 0xa1;
    memcpy(frame->bytes + 1, report->bytes, consumer ? 2 : 8);
    radio->head = (uint8_t)((radio->head + 1) % 8);
    --radio->count;
    return claim(radio, frame, consumer ? GMK_RADIO_TX_CONSUMER : GMK_RADIO_TX_KEYBOARD, consumer ? 4 : 10);
}
void gmk_radio_core_sent(struct gmk_radio_core *radio, uint32_t now, bool success) {
    uint8_t kind = radio->inflight;
    if (!kind) return;
    radio->inflight = GMK_RADIO_TX_NONE;
    if (!success) {
        ++radio->errors;
        radio->connection = GMK_RADIO_UNKNOWN;
        radio->waiting = radio->pending_pair = radio->pending_select = radio->accept_status = false;
        radio->clear_requested                                                              = true;
        radio->faulted                                                                      = true;
        discard(radio);
        return;
    }
    if (kind == GMK_RADIO_TX_SELECT || kind == GMK_RADIO_TX_PAIR) {
        if (kind == GMK_RADIO_TX_SELECT) {
            radio->active         = radio->tx_channel;
            radio->pending_select = radio->selected != radio->active;
            radio->accept_status  = !radio->pending_select;
            ++radio->selections;
        } else {
            radio->pending_pair = false;
            radio->connection   = GMK_RADIO_PAIRING;
        }
        if (!radio->retransmit) radio->retries = 0;
        radio->waiting      = true;
        radio->sent_at      = now;
        radio->last_control = kind;
    } else {
        if (kind == GMK_RADIO_TX_RELEASE_KEYBOARD) radio->release &= (uint8_t)~1U;
        if (kind == GMK_RADIO_TX_RELEASE_CONSUMER) radio->release &= (uint8_t)~2U;
        ++radio->reports;
    }
}
