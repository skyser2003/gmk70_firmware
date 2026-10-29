/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "battery_status.h"
#include "protocol.h"
#include <string.h>

bool gmk_battery_status_packet(const uint8_t request[32]) {
    if (request[0] != GMK_UPDATE_COMMAND || request[1] != 0x72 || request[2] != 1 || request[3] != 0 || request[4] != 0x21 || request[5] || request[6] || request[7] || !gmk_read_u32(request + 8)) return false;
    for (unsigned i = 12; i < GMK_PACKET_SIZE; ++i)
        if (request[i]) return false;
    return true;
}

void gmk_battery_status_reply(const uint8_t request[32], const struct gmk_battery_status *status, uint8_t response[32]) {
    bool valid = status->valid && status->percentage <= 100 && status->age_ms <= 10000;
    memset(response, 0, GMK_PACKET_SIZE);
    memcpy(response, request, 5);
    memcpy(response + 8, request + 8, 4);
    response[5] = GMK_OK;
    response[7] = 3;
    gmk_write_u32(response + 12, 0x54544142);
    response[16] = valid ? status->percentage : UINT8_MAX;
    response[17] = (valid ? 1U : 0U) | (status->cable ? 2U : 0U) | (status->cable && status->full ? 4U : 0U);
    gmk_write_u32(response + 20, status->age_ms);
    gmk_write_u32(response + 24, status->queries);
    gmk_write_u32(response + 28, status->replies);
}
