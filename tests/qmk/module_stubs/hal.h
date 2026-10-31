// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
typedef uint32_t eventflags_t;
typedef uint32_t eventmask_t;
typedef struct {
    eventflags_t flags;
} event_listener_t;
typedef struct {
    uint8_t unused;
} event_source_t;
typedef struct {
    event_source_t event;
} test_serial_driver_t;
extern test_serial_driver_t SD3;
#define UART_DRIVER SD3
#define SD_OVERRUN_ERROR 1U
#define SD_PARITY_ERROR 2U
#define SD_FRAMING_ERROR 4U
#define SD_BREAK_DETECTED 8U
#define SD_NOISE_ERROR 16U
#define CHN_INPUT_AVAILABLE 32U
#define SD_QUEUE_FULL_ERROR 64U
#define chnGetEventSource(driver) (&(driver)->event)
void         chEvtRegisterMaskWithFlags(event_source_t *source, event_listener_t *listener, eventmask_t events, eventflags_t flags);
eventflags_t chEvtGetAndClearFlags(event_listener_t *listener);
