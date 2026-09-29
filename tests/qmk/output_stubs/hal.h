// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#define TRUE 1
#define FALSE 0
#define SPI_SUPPORTS_CIRCULAR FALSE

enum { SPI_READY = 1, SPI_ACTIVE = 2 };
typedef struct {
    void (*end_cb)(void);
    unsigned port;
    unsigned pad;
    unsigned phase;
    unsigned polarity;
    unsigned divisor;
} SPIConfig;
typedef struct {
    unsigned state;
    struct {
        size_t tx_len;
        size_t rx_len;
    } xfer;
} SPIDriver;
extern SPIDriver SPIDM2;
void             spiAcquireBus(SPIDriver *driver);
void             spiStart(SPIDriver *driver, const SPIConfig *config);
void             spiSelect(SPIDriver *driver);
void             spiStartSend(SPIDriver *driver, size_t length, const void *data);
void             spiSend(SPIDriver *driver, size_t length, const void *data);
