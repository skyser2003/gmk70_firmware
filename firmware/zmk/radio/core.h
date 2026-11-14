/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum gmk_host_channel { GMK_HOST_USB, GMK_HOST_BT1, GMK_HOST_BT2, GMK_HOST_BT3, GMK_HOST_BT4, GMK_HOST_BT5, GMK_HOST_DONGLE };
enum gmk_radio_connection { GMK_RADIO_UNKNOWN, GMK_RADIO_PAIRING, GMK_RADIO_CONNECTED, GMK_RADIO_DISCONNECTED, GMK_RADIO_REJECTED };
enum gmk_radio_tx_kind { GMK_RADIO_TX_NONE, GMK_RADIO_TX_SELECT, GMK_RADIO_TX_PAIR, GMK_RADIO_TX_KEYBOARD, GMK_RADIO_TX_CONSUMER, GMK_RADIO_TX_RELEASE_KEYBOARD, GMK_RADIO_TX_RELEASE_CONSUMER, GMK_RADIO_TX_NKRO, GMK_RADIO_TX_RELEASE_NKRO, GMK_RADIO_TX_MOUSE, GMK_RADIO_TX_RELEASE_MOUSE, GMK_RADIO_TX_SLEEP_BT, GMK_RADIO_TX_SLEEP_DONGLE, GMK_RADIO_TX_CHARGER };
struct gmk_radio_frame {
    uint8_t bytes[16];
    uint8_t length;
};
struct gmk_radio_report {
    uint8_t bytes[8];
    uint8_t overflow[14];
    uint8_t kind;
};
struct gmk_radio_core {
    /* Private RAM only: short metadata offsets keep Cortex-M3 loads compact. */
    uint8_t                 head, count, selected, active, last_bt, retries, release, inflight, tx_channel, last_control, connection, sleep_pending;
    bool                    waiting, pending_select, pending_pair, accept_status, retransmit, clear_requested, faulted, overflow_pending, obsolete, draining, resuming;
    uint8_t                 charge_mode, charge_sent, connect_attempts;
    uint32_t                sent_at, errors, selections, reports, charge_at, connect_at;
    struct gmk_radio_report queue[8];
};
void gmk_radio_core_init(struct gmk_radio_core *radio);
int  gmk_radio_core_select(struct gmk_radio_core *radio, uint8_t channel, bool pair);
/* An idle, paused same-host wake retains the first native input snapshots. */
void     gmk_radio_core_wake(struct gmk_radio_core *radio);
int      gmk_radio_core_report(struct gmk_radio_core *radio, bool consumer, const uint8_t bytes[8]);
int      gmk_radio_core_mouse(struct gmk_radio_core *radio, const uint8_t bytes[5]);
int      gmk_radio_core_nkro(struct gmk_radio_core *radio, const uint8_t keyboard[8], const uint8_t overflow[14]);
void     gmk_radio_core_release(struct gmk_radio_core *radio, bool consumer);
void     gmk_radio_core_release_mouse(struct gmk_radio_core *radio);
void     gmk_radio_core_enable_sleep(struct gmk_radio_core *radio);
void     gmk_radio_core_charger(struct gmk_radio_core *radio, bool cable, bool full);
bool     gmk_radio_core_idle(const struct gmk_radio_core *radio);
uint32_t gmk_radio_core_delay(const struct gmk_radio_core *radio, uint32_t now);
bool     gmk_radio_core_control_busy(const struct gmk_radio_core *radio);
/* Allow a due query ahead of queued HID, never inside controls/releases/NKRO. */
bool gmk_radio_core_battery_ready(const struct gmk_radio_core *radio, uint32_t now);
void gmk_radio_core_notification(struct gmk_radio_core *radio, uint8_t command, uint8_t value);
void gmk_radio_core_ack(struct gmk_radio_core *radio);
bool gmk_radio_core_next(struct gmk_radio_core *radio, uint32_t now, struct gmk_radio_frame *frame);
void gmk_radio_core_sent(struct gmk_radio_core *radio, uint32_t now, bool success);
