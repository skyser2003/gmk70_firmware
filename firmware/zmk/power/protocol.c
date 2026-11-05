/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "protocol.h"
#include "../wired/wired_protocol.h"
#include <string.h>

enum { HEADER_SIZE = 8, CRC_OFFSET = 31, KNOWN_FLAGS = 7 };

static bool header_valid(const uint8_t packet[32]) {
    return packet[0] == 'P' && packet[1] == 'W' && packet[2] == 'R' && packet[3] == GMK_POWER_PROTOCOL_VERSION && packet[4] <= GMK_POWER_CANCEL && packet[5] <= 1 && (packet[6] || packet[7]) && packet[CRC_OFFSET] == gmk_wire_crc8(packet, CRC_OFFSET);
}
static bool reserved_zero(const uint8_t packet[32], unsigned first) {
    for (unsigned offset = first; offset < CRC_OFFSET; ++offset)
        if (packet[offset]) return false;
    return true;
}
bool gmk_power_protocol_request_encode(uint8_t out[32], const struct gmk_power_command *command) {
    const struct gmk_power_command value = *command;
    if ((unsigned)value.operation > GMK_POWER_CANCEL || !value.session) return false;
    memset(out, 0, GMK_POWER_PACKET_SIZE);
    out[0]          = 'P';
    out[1]          = 'W';
    out[2]          = 'R';
    out[3]          = GMK_POWER_PROTOCOL_VERSION;
    out[4]          = (uint8_t)value.operation;
    out[5]          = value.critical ? 1 : 0;
    out[6]          = (uint8_t)value.session;
    out[7]          = (uint8_t)(value.session >> 8);
    out[CRC_OFFSET] = gmk_wire_crc8(out, CRC_OFFSET);
    return true;
}
bool gmk_power_protocol_request_decode(const uint8_t in[32], struct gmk_power_command *command) {
    if (!header_valid(in) || !reserved_zero(in, HEADER_SIZE)) return false;
    *command = (struct gmk_power_command){.operation = (enum gmk_power_operation)in[4], .session = (uint16_t)((uint16_t)in[6] | (uint16_t)((uint16_t)in[7] << 8)), .critical = in[5] != 0};
    return true;
}
bool gmk_power_protocol_reply_encode(uint8_t out[32], const uint8_t request[32], const struct gmk_power_reply *reply) {
    struct gmk_power_command     command;
    const struct gmk_power_reply value = *reply;
    if (!gmk_power_protocol_request_decode(request, &command) || (unsigned)value.phase > GMK_POWER_PHASE_COMMITTED || (value.flags & (uint8_t)~KNOWN_FLAGS)) return false;
    memmove(out, request, HEADER_SIZE);
    memset(out + HEADER_SIZE, 0, GMK_POWER_PACKET_SIZE - HEADER_SIZE);
    out[8]          = (uint8_t)value.phase;
    out[9]          = value.flags;
    out[CRC_OFFSET] = gmk_wire_crc8(out, CRC_OFFSET);
    return true;
}
bool gmk_power_protocol_reply_decode(const uint8_t request[32], const uint8_t response[32], struct gmk_power_reply *reply) {
    struct gmk_power_command command;
    if (!gmk_power_protocol_request_decode(request, &command) || !header_valid(response) || memcmp(request, response, HEADER_SIZE) || response[8] > GMK_POWER_PHASE_COMMITTED || (response[9] & (uint8_t)~KNOWN_FLAGS) || !reserved_zero(response, 10)) return false;
    *reply = (struct gmk_power_reply){.phase = (enum gmk_power_phase)response[8], .flags = response[9]};
    return true;
}
