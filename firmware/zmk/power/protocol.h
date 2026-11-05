/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { GMK_POWER_PACKET_SIZE = 32, GMK_POWER_PROTOCOL_VERSION = 1 };
enum gmk_power_operation { GMK_POWER_PROBE = 0, GMK_POWER_PREPARE = 1, GMK_POWER_STATUS = 2, GMK_POWER_COMMIT = 3, GMK_POWER_CANCEL = 4 };
enum gmk_power_phase { GMK_POWER_PHASE_IDLE = 0, GMK_POWER_PHASE_REQUESTED = 1, GMK_POWER_PHASE_READY = 2, GMK_POWER_PHASE_COMMITTED = 3 };
enum gmk_power_flags { GMK_POWER_FLAG_USB = 1, GMK_POWER_FLAG_BUSY = 2, GMK_POWER_FLAG_ACTIVE = 4 };
struct gmk_power_command {
    enum gmk_power_operation operation;
    uint16_t                 session;
    bool                     critical;
};
struct gmk_power_reply {
    enum gmk_power_phase phase;
    uint8_t              flags;
};
/* Encoding/decoding failures leave caller outputs unchanged. Reply validation
 * binds all eight header bytes to a valid exact request, including its session.
 * The coordinator owns allowed phase transitions; this codec has no state. */
bool gmk_power_protocol_request_encode(uint8_t out[32], const struct gmk_power_command *command);
bool gmk_power_protocol_request_decode(const uint8_t in[32], struct gmk_power_command *command);
bool gmk_power_protocol_reply_encode(uint8_t out[32], const uint8_t request[32], const struct gmk_power_reply *reply);
bool gmk_power_protocol_reply_decode(const uint8_t request[32], const uint8_t response[32], struct gmk_power_reply *reply);
