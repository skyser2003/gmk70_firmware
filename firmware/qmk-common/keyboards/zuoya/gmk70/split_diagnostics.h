// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SPLIT_DIAG_STATE = 1,
    SPLIT_DIAG_MANUAL,
    SPLIT_DIAG_HOST,
    SPLIT_DIAG_STOP_ENTER,
    SPLIT_DIAG_STOP_EXIT,
    SPLIT_DIAG_DEVICE,
    SPLIT_DIAG_SLEEP_REASON,
    SPLIT_DIAG_RPC_RESULT,
    SPLIT_DIAG_CONTROL_TX,
    SPLIT_DIAG_CONTROL_RX,
    SPLIT_DIAG_PEER,
    SPLIT_DIAG_LOCAL,
    SPLIT_DIAG_LINK,
    SPLIT_DIAG_SPI_SLOW, // Reserved legacy event; protocol 2 has no SPI instrumentation.
    SPLIT_DIAG_LOOP_GAP,
    SPLIT_DIAG_TIMEOUT,
    SPLIT_DIAG_STOP_CANCEL,
} split_diag_event_t;

typedef enum {
    SPLIT_DIAG_RADIO_DISCONNECTED = 1,
    SPLIT_DIAG_RADIO_IDLE,
    SPLIT_DIAG_LOW_BATTERY,
    SPLIT_DIAG_HOST_SUSPEND,
    SPLIT_DIAG_USB_SPECIAL,
} split_diag_sleep_reason_t;

#ifdef GMK70_SPLIT_DIAGNOSTICS
typedef struct {
    uint8_t  status; // state:0..1, manual:2, A5:3, A8:4, reserved:5, low battery sleep:6, master:7
    uint8_t  device;
    uint8_t  radio;
    uint8_t  wake;
    uint8_t  pins; // A7, A15, C15, C14, B9 in bits 0..4
    bool     left;
    bool     connected; // QMK's split connection flag, meaningful on master only
    uint8_t  battery, sleep_selector;
    uint32_t timeout;
} split_diag_sample_t;

// Platform snapshot must not block, acquire a split mutex, or write hardware.
split_diag_sample_t split_diagnostics_sample(void);
void                split_diagnostics_loop(void);
void                split_diagnostics_rpc_begin(uint8_t command);
void                split_diagnostics_rpc_end(uint8_t command, bool success, const uint8_t *peer);
bool                split_diagnostics_command(uint8_t *data, uint8_t length);
void                split_diagnostics_event(split_diag_event_t event, uint8_t a, uint8_t b, uint8_t c);
void                split_diagnostics_reply(uint8_t body[4]);
void                split_diagnostics_received(uint8_t command);
void                split_diagnostics_sleep_accepted(bool manual, uint32_t idle_ms, uint8_t device);
#else
static inline void split_diagnostics_loop(void) {}
static inline void split_diagnostics_event(split_diag_event_t event, uint8_t a, uint8_t b, uint8_t c) {
    (void)event;
    (void)a;
    (void)b;
    (void)c;
}
static inline void split_diagnostics_reply(uint8_t body[4]) {
    (void)body;
}
static inline void split_diagnostics_received(uint8_t command) {
    (void)command;
}
#endif

#ifdef GMK70_PERSISTENT_DIAGNOSTICS
void split_diagnostics_checkpoint(uint8_t trigger);
#else
static inline void split_diagnostics_checkpoint(uint8_t trigger) {
    (void)trigger;
}
#endif
