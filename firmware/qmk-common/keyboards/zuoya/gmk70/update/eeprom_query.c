// SPDX-License-Identifier: GPL-2.0-or-later
#include "eeprom_query.h"
#include "eeprom.h"
#include <string.h>

static uint32_t logical_crc(void) {
    uint32_t crc = UINT32_MAX;
    uint8_t  block[GMK_DATA_SIZE];
    // Read the logical EEPROM cache through QMK, not its wear-leveling backing.
    // Avoid allocating a second 4 KB copy in the keyboard's limited RAM.
    for (uint32_t offset = 0; offset < TOTAL_EEPROM_BYTE_COUNT; offset += sizeof(block)) {
        size_t count = TOTAL_EEPROM_BYTE_COUNT - offset;
        if (count > sizeof(block)) count = sizeof(block);
        eeprom_read_block(block, (const void *)(uintptr_t)offset, count);
        for (size_t i = 0; i < count; ++i) {
            crc ^= block[i];
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void gmk_eeprom_query(const uint8_t request[GMK_PACKET_SIZE], gmk_update_result_t result, uint8_t response[GMK_PACKET_SIZE]) {
    memset(response, 0, GMK_PACKET_SIZE);
    memcpy(response, request, 5);
    memcpy(response + 8, request + 8, 8); // Correlation token and logical offset.
    if (!gmk_packet_valid(request) || (request[4] != GMK_EEPROM_INFO && request[4] != GMK_EEPROM_READ)) result = GMK_BAD_REQUEST;
    uint32_t offset = gmk_read_u32(request + 12);
    if (result == GMK_OK && request[4] == GMK_EEPROM_READ && (offset >= TOTAL_EEPROM_BYTE_COUNT || request[5] > TOTAL_EEPROM_BYTE_COUNT - offset)) result = GMK_BAD_OFFSET;
    response[5] = result;
    if (result != GMK_OK) return;
    if (request[4] == GMK_EEPROM_INFO) {
        gmk_write_u32(response + 16, TOTAL_EEPROM_BYTE_COUNT);
        gmk_write_u32(response + 20, logical_crc());
        gmk_write_u32(response + 24, GMK_EEPROM_ABI);
        gmk_write_u32(response + 28, GMK_EEPROM_LAYOUT);
    } else {
        response[6] = request[5];
        eeprom_read_block(response + 16, (const void *)(uintptr_t)offset, request[5]);
    }
}
