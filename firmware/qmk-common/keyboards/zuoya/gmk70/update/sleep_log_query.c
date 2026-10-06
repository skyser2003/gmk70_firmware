// SPDX-License-Identifier: GPL-2.0-or-later
#include "sleep_log_query.h"
#include "sleep_log.h"
#include <string.h>

void gmk_sleep_log_query(const uint8_t request[GMK_PACKET_SIZE], gmk_update_result_t result, uint8_t response[GMK_PACKET_SIZE]) {
    memset(response, 0, GMK_PACKET_SIZE);
    memcpy(response, request, 5);
    memcpy(response + 8, request + 8, 8);
    if (!gmk_packet_valid(request) || (request[4] != GMK_SLEEP_LOG_INFO && request[4] != GMK_SLEEP_LOG_READ)) result = GMK_BAD_REQUEST;
    uint32_t offset = gmk_read_u32(request + 12), status = sleep_log_status();
    uint32_t size = status & SLEEP_LOG_READY ? SLEEP_LOG_SIZE : 0;
    if (result == GMK_OK && request[4] == GMK_SLEEP_LOG_READ) {
        if (offset >= size || request[5] > size - offset)
            result = GMK_BAD_OFFSET;
        else if (!sleep_log_read(offset, response + 16, request[5]))
            result = GMK_FLASH_ERROR;
        else
            response[6] = request[5];
    } else if (result == GMK_OK) {
        uint32_t crc = UINT32_MAX;
        uint8_t  block[GMK_DATA_SIZE];
        for (uint32_t pos = 0; pos < size; pos += sizeof(block)) {
            if (!sleep_log_read(pos, block, sizeof(block))) {
                result = GMK_FLASH_ERROR;
                break;
            }
            for (unsigned i = 0; i < sizeof(block); ++i) {
                crc ^= block[i];
                for (unsigned b = 0; b < 8; ++b)
                    crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
            }
        }
        gmk_write_u32(response + 16, size);
        gmk_write_u32(response + 20, ~crc);
        gmk_write_u32(response + 24, SLEEP_LOG_FORMAT);
        gmk_write_u32(response + 28, status);
    }
    response[5] = result;
    if (result != GMK_OK) memset(response + 16, 0, GMK_DATA_SIZE);
}
