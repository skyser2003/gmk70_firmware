/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum gmk_host_channel { GMK_HOST_USB, GMK_HOST_BT1, GMK_HOST_BT2, GMK_HOST_BT3, GMK_HOST_BT4, GMK_HOST_BT5, GMK_HOST_DONGLE };
enum gmk_radio_connection { GMK_RADIO_UNKNOWN, GMK_RADIO_PAIRING, GMK_RADIO_CONNECTED, GMK_RADIO_DISCONNECTED, GMK_RADIO_REJECTED };
enum gmk_radio_tx_kind { GMK_RADIO_TX_NONE, GMK_RADIO_TX_SELECT, GMK_RADIO_TX_PAIR, GMK_RADIO_TX_KEYBOARD, GMK_RADIO_TX_CONSUMER, GMK_RADIO_TX_RELEASE_KEYBOARD, GMK_RADIO_TX_RELEASE_CONSUMER };
struct gmk_radio_frame {
    uint8_t bytes[10];
    uint8_t length;
};
struct gmk_radio_report {
    uint8_t bytes[8];
    bool    consumer;
};
struct gmk_radio_core {
    uint32_t                sent_at, errors, selections, reports;
    struct gmk_radio_report queue[8];
    uint8_t                 head, count, selected, active, last_bt, retries, release, inflight, tx_channel, last_control, connection;
    bool                    waiting, pending_select, pending_pair, accept_status, retransmit, clear_requested, faulted;
};
void gmk_radio_core_init(struct gmk_radio_core *radio);
int  gmk_radio_core_select(struct gmk_radio_core *radio, uint8_t channel, bool pair);
int  gmk_radio_core_report(struct gmk_radio_core *radio, bool consumer, const uint8_t bytes[8]);
void gmk_radio_core_release(struct gmk_radio_core *radio, bool consumer);
void gmk_radio_core_notification(struct gmk_radio_core *radio, uint8_t command, uint8_t value);
void gmk_radio_core_ack(struct gmk_radio_core *radio);
bool gmk_radio_core_next(struct gmk_radio_core *radio, uint32_t now, struct gmk_radio_frame *frame);
void gmk_radio_core_sent(struct gmk_radio_core *radio, uint32_t now, bool success);
