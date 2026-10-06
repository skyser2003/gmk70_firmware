// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    SLEEP_LOG_BASE         = 0x10000, // Beyond the settings driver's entire 64 KB erase block.
    SLEEP_LOG_SECTOR       = 4096,
    SLEEP_LOG_SIZE         = 2 * SLEEP_LOG_SECTOR,
    SLEEP_LOG_PAGE         = 256,
    SLEEP_LOG_EVENTS       = 16,
    SLEEP_LOG_FORMAT       = 1,
    SLEEP_LOG_READY        = 1,
    SLEEP_LOG_OWNED        = 2,
    SLEEP_LOG_IO_ERROR     = 4,
    SLEEP_LOG_BEFORE_STOP  = 1,
    SLEEP_LOG_AFTER_WAKE   = 2,
    SLEEP_LOG_PEER_FAILURE = 3,
};

// Platform adapter: main thread only; addresses are absolute external SPI flash.
bool     sleep_log_storage_init(void);
uint32_t sleep_log_storage_id(void);
bool     sleep_log_storage_read(uint32_t address, void *data, size_t size);
bool     sleep_log_storage_write(uint32_t address, const void *data, size_t size);
bool     sleep_log_storage_erase(uint32_t address);

void     sleep_log_init(void);
bool     sleep_log_append(uint8_t page[SLEEP_LOG_PAGE]);
bool     sleep_log_read(uint32_t offset, void *data, size_t size);
uint32_t sleep_log_status(void);
uint32_t sleep_log_crc(const void *data, size_t size);
bool     sleep_log_valid(const uint8_t page[SLEEP_LOG_PAGE]);
