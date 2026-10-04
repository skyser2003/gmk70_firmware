// SPDX-License-Identifier: GPL-2.0-or-later
#include "image.h"
#include <string.h>

uint32_t gmk_read_u32(const uint8_t *data) {
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

void gmk_write_u32(uint8_t *data, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        data[i] = (uint8_t)(value >> (8 * i));
}

uint32_t gmk_crc32(const uint8_t *data, size_t size) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

bool gmk_image_bounds(uint32_t size) {
    return size >= 1024 && size <= GMK_APP_SIZE && size % 4 == 0;
}

bool gmk_image_vectors(const uint8_t *data, uint32_t size) {
    if (!gmk_image_bounds(size)) return false;
    uint32_t stack = gmk_read_u32(data);
    uint32_t reset = gmk_read_u32(data + 4);
    return stack > GMK_RAM_BASE && stack <= GMK_RAM_END && stack % 8 == 0 && (reset & 1U) && reset >= GMK_APP_BASE && reset < GMK_APP_BASE + size;
}

bool gmk_image_valid(uint32_t address, gmk_image_t image) {
    if ((address != GMK_APP_BASE && address != GMK_STAGE_BASE) || !gmk_image_bounds(image.size)) return false;
    const uint8_t *data = gmk_flash_view(address);
    return gmk_image_vectors(data, image.size) && gmk_crc32(data, image.size) == image.crc;
}

void gmk_manifest_encode(uint8_t page[GMK_PAGE_SIZE], gmk_image_t image) {
    memset(page, 0xFF, GMK_PAGE_SIZE);
    gmk_write_u32(page, 0x47554D31);
    gmk_write_u32(page + 4, GMK_UPDATE_ABI);
    gmk_write_u32(page + 8, GMK_APP_BASE);
    gmk_write_u32(page + 12, image.size);
    gmk_write_u32(page + 16, image.crc);
    gmk_write_u32(page + 20, ~image.size);
    gmk_write_u32(page + 24, ~image.crc);
    gmk_write_u32(page + 28, gmk_crc32(page, 28));
}

bool gmk_manifest_decode(const uint8_t *page, gmk_image_t *image) {
    image->size = gmk_read_u32(page + 12);
    image->crc  = gmk_read_u32(page + 16);
    return gmk_read_u32(page) == 0x47554D31 && gmk_read_u32(page + 4) == GMK_UPDATE_ABI && gmk_read_u32(page + 8) == GMK_APP_BASE && gmk_image_bounds(image->size) && gmk_read_u32(page + 20) == ~image->size && gmk_read_u32(page + 24) == ~image->crc && gmk_read_u32(page + 28) == gmk_crc32(page, 28);
}

bool gmk_manifest_clear(void) {
    uint8_t page[GMK_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    return gmk_flash_page(GMK_COMMIT_BASE, page);
}

bool gmk_install_pending(void) {
    gmk_image_t image;
    if (!gmk_manifest_decode(gmk_flash_view(GMK_COMMIT_BASE), &image)) return true;
    // Validate the complete staged image before touching a single app page.
    if (!gmk_image_valid(GMK_STAGE_BASE, image)) return false;
    for (uint32_t offset = 0; offset < image.size; offset += GMK_PAGE_SIZE) {
        const uint8_t *source = gmk_flash_view(GMK_STAGE_BASE + offset);
        // Repeating after a power cut is safe; already matching pages are skipped.
        if (memcmp(gmk_flash_view(GMK_APP_BASE + offset), source, GMK_PAGE_SIZE) != 0 && !gmk_flash_page(GMK_APP_BASE + offset, source)) return false;
    }
    if (!gmk_image_valid(GMK_APP_BASE, image)) return false;
    // The installed image is complete before the commit record is invalidated.
    return gmk_manifest_clear();
}
