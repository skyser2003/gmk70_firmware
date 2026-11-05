/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "core.h"

struct gmk_radio_status {
    uint32_t errors, selections, reports;
    uint8_t  selected, active, connection, queued;
    bool     waiting, available;
};
bool gmk_radio_selected(void);
int  gmk_radio_select(uint8_t channel, bool pair);
int  gmk_radio_submit_keyboard(const uint8_t report[8]);
/* Snapshot both halves together; each A1 is followed by its matching A2. */
int  gmk_radio_submit_nkro(const uint8_t report[8], const uint8_t overflow[14]);
int  gmk_radio_submit_consumer(uint16_t usage);
int  gmk_radio_submit_mouse(const uint8_t report[5]);
void gmk_radio_release(bool consumer);
void gmk_radio_release_mouse(void);
void gmk_radio_clear_hid(void);
void gmk_radio_poll(void);
void gmk_radio_get(struct gmk_radio_status *out);
void gmk_radio_notification(uint8_t command, uint8_t value);
void gmk_radio_ack(void);
void gmk_radio_charger_state(bool cable, bool full);
bool gmk_radio_next(uint32_t now, struct gmk_radio_frame *frame);
void gmk_radio_sent(uint32_t now, bool success);
bool gmk_radio_control_busy(void);
bool gmk_radio_idle(void);
/* Called by the service owner after UART idle; admission rejects new input. */
bool    gmk_radio_power_quiesce(bool enabled);
uint8_t gmk_radio_last_bt(void);
int     gmk_radio_restore_last_bt(uint8_t channel);
int     gmk_radio_hold(uint8_t channel, uint32_t position);
void    gmk_radio_unhold(uint32_t position);
#ifdef CONFIG_GMK70_RADIO_QUALIFICATION
int gmk_radio_test_marker(void);
#endif
