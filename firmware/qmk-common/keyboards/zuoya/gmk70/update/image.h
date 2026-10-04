// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    GMK_UPDATE_ABI  = 1,
    GMK_FLASH_BASE  = 0x08000000,
    GMK_BOOT_SIZE   = 0x2000,
    GMK_APP_BASE    = GMK_FLASH_BASE + GMK_BOOT_SIZE,
    GMK_APP_SIZE    = 0x1E000,
    GMK_STAGE_BASE  = 0x08020000,
    GMK_COMMIT_BASE = 0x0803FF00,
    GMK_PAGE_SIZE   = 256,
    GMK_RAM_BASE    = 0x20000000,
    // Retain QMK's existing 28 KB SRAM layout, including its bootloader magic.
    GMK_RAM_END = 0x20007000,
};

typedef struct {
    uint32_t size;
    uint32_t crc;
} gmk_image_t;

// Platform adapters must restrict writes to staging/metadata in the application,
// and to application/metadata in the resident installer. Never write the stub.
const uint8_t *gmk_flash_view(uint32_t address);
uint32_t       gmk_running_size(void);
bool           gmk_flash_page(uint32_t address, const uint8_t data[GMK_PAGE_SIZE]);
uint32_t       gmk_read_u32(const uint8_t *data);
void           gmk_write_u32(uint8_t *data, uint32_t value);
uint32_t       gmk_crc32(const uint8_t *data, size_t size);
bool           gmk_image_bounds(uint32_t size);
bool           gmk_image_vectors(const uint8_t *data, uint32_t size);
bool           gmk_image_valid(uint32_t address, gmk_image_t image);
void           gmk_manifest_encode(uint8_t page[GMK_PAGE_SIZE], gmk_image_t image);
bool           gmk_manifest_decode(const uint8_t *page, gmk_image_t *image);
bool           gmk_manifest_clear(void);
bool           gmk_install_pending(void);
