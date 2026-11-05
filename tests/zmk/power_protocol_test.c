/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "protocol.h"
#include "wired_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t request[32]  = {'P', 'W', 'R', 1, 1, 1, 0x34, 0x12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x48};
static const uint8_t response[32] = {'P', 'W', 'R', 1, 1, 1, 0x34, 0x12, 2, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xcb};
static void          checksum(uint8_t packet[32]) {
    packet[31] = gmk_wire_crc8(packet, 31);
}
static void assert_request_rejected(const uint8_t packet[32]) {
    struct gmk_power_command out = {.operation = GMK_POWER_CANCEL, .session = 999, .critical = false};
    assert(!gmk_power_protocol_request_decode(packet, &out));
    assert(out.operation == GMK_POWER_CANCEL && out.session == 999 && !out.critical);
}
static void assert_reply_rejected(const uint8_t packet[32]) {
    struct gmk_power_reply out = {.phase = GMK_POWER_PHASE_COMMITTED, .flags = 7};
    assert(!gmk_power_protocol_reply_decode(request, packet, &out));
    assert(out.phase == GMK_POWER_PHASE_COMMITTED && out.flags == 7);
}
static void exact_wire_layout_and_checksum_match_known_packets(void) {
    uint8_t                        encoded[32];
    const struct gmk_power_command command = {.operation = GMK_POWER_PREPARE, .session = 0x1234, .critical = true};
    assert(gmk_power_protocol_request_encode(encoded, &command));
    assert(!memcmp(encoded, request, sizeof(encoded)));
    const struct gmk_power_reply reply = {.phase = GMK_POWER_PHASE_READY, .flags = GMK_POWER_FLAG_USB | GMK_POWER_FLAG_ACTIVE};
    assert(gmk_power_protocol_reply_encode(encoded, request, &reply));
    assert(!memcmp(encoded, response, sizeof(encoded)));
}
static void every_operation_phase_and_known_flag_combination_roundtrips(void) {
    const uint16_t sessions[] = {1, 0x100, 0xffff, 1};
    for (unsigned operation = 0; operation <= 4; ++operation)
        for (unsigned critical = 0; critical <= 1; ++critical)
            for (unsigned sample = 0; sample < sizeof(sessions) / sizeof(sessions[0]); ++sample) {
                const struct gmk_power_command command = {.operation = (enum gmk_power_operation)operation, .session = sessions[sample], .critical = critical != 0};
                uint8_t                        encoded[32], reply_bytes[32];
                struct gmk_power_command       decoded;
                assert(gmk_power_protocol_request_encode(encoded, &command));
                assert(gmk_power_protocol_request_decode(encoded, &decoded));
                assert(decoded.operation == command.operation && decoded.session == command.session && decoded.critical == command.critical);
                for (unsigned phase = 0; phase <= 3; ++phase)
                    for (uint8_t flags = 0; flags <= 7; ++flags) {
                        const struct gmk_power_reply reply = {.phase = (enum gmk_power_phase)phase, .flags = flags};
                        struct gmk_power_reply       received;
                        assert(gmk_power_protocol_reply_encode(reply_bytes, encoded, &reply));
                        assert(gmk_power_protocol_reply_decode(encoded, reply_bytes, &received));
                        assert(received.phase == reply.phase && received.flags == flags);
                    }
            }
}
static void every_single_bit_corruption_is_rejected(void) {
    for (unsigned offset = 0; offset < 32; ++offset)
        for (unsigned bit = 0; bit < 8; ++bit) {
            uint8_t changed[32];
            memcpy(changed, request, sizeof(changed));
            changed[offset] ^= (uint8_t)(1U << bit);
            assert_request_rejected(changed);
            memcpy(changed, response, sizeof(changed));
            changed[offset] ^= (uint8_t)(1U << bit);
            assert_reply_rejected(changed);
        }
}
static void invalid_semantics_fail_even_with_a_correct_checksum(void) {
    uint8_t changed[32];
    for (unsigned offset = 8; offset < 31; ++offset) {
        memcpy(changed, request, sizeof(changed));
        changed[offset] = 1;
        checksum(changed);
        assert_request_rejected(changed);
    }
    const uint8_t offsets[] = {0, 1, 2, 3, 4, 5};
    const uint8_t invalid[] = {'Q', 'V', 'S', 2, 5, 2};
    for (unsigned index = 0; index < sizeof(offsets); ++index) {
        memcpy(changed, request, sizeof(changed));
        changed[offsets[index]] = invalid[index];
        checksum(changed);
        assert_request_rejected(changed);
    }
    memcpy(changed, request, sizeof(changed));
    changed[6] = changed[7] = 0;
    checksum(changed);
    assert_request_rejected(changed);
    for (unsigned offset = 10; offset < 31; ++offset) {
        memcpy(changed, response, sizeof(changed));
        changed[offset] = 1;
        checksum(changed);
        assert_reply_rejected(changed);
    }
    memcpy(changed, response, sizeof(changed));
    changed[8] = 4;
    checksum(changed);
    assert_reply_rejected(changed);
    for (unsigned bit = 3; bit < 8; ++bit) {
        memcpy(changed, response, sizeof(changed));
        changed[9] |= (uint8_t)(1U << bit);
        checksum(changed);
        assert_reply_rejected(changed);
    }
}
static void replies_are_bound_to_exact_session_operation_critical_and_version(void) {
    for (unsigned offset = 0; offset < 8; ++offset) {
        uint8_t changed[32];
        memcpy(changed, response, sizeof(changed));
        changed[offset] ^= 1;
        checksum(changed);
        assert_reply_rejected(changed);
    }
    uint8_t changed_request[32];
    memcpy(changed_request, request, sizeof(changed_request));
    changed_request[8] = 1;
    checksum(changed_request);
    struct gmk_power_reply out;
    assert(!gmk_power_protocol_reply_decode(changed_request, response, &out));
    struct gmk_power_command     command = {.operation = GMK_POWER_STATUS, .session = UINT16_MAX};
    const struct gmk_power_reply ready   = {.phase = GMK_POWER_PHASE_READY};
    uint8_t                      previous[32], wrapped[32], previous_reply[32];
    assert(gmk_power_protocol_request_encode(previous, &command));
    assert(gmk_power_protocol_reply_encode(previous_reply, previous, &ready));
    command.session = 1;
    assert(gmk_power_protocol_request_encode(wrapped, &command));
    assert(!gmk_power_protocol_reply_decode(wrapped, previous_reply, &out));
}
static void invalid_encoders_preserve_outputs_and_aliasing_remains_supported(void) {
    uint8_t out[32];
    memset(out, 0xa5, sizeof(out));
    struct gmk_power_command command = {.operation = GMK_POWER_PREPARE, .session = 0};
    assert(!gmk_power_protocol_request_encode(out, &command));
    command.session   = 1;
    command.operation = (enum gmk_power_operation)5;
    assert(!gmk_power_protocol_request_encode(out, &command));
    command.operation = (enum gmk_power_operation) - 1;
    assert(!gmk_power_protocol_request_encode(out, &command));
    struct gmk_power_reply reply = {.phase = (enum gmk_power_phase)4, .flags = 0};
    assert(!gmk_power_protocol_reply_encode(out, request, &reply));
    reply.phase = (enum gmk_power_phase) - 1;
    assert(!gmk_power_protocol_reply_encode(out, request, &reply));
    reply.phase = GMK_POWER_PHASE_READY;
    reply.flags = 8;
    assert(!gmk_power_protocol_reply_encode(out, request, &reply));
    reply.flags           = 5;
    uint8_t malformed[32] = {0};
    assert(!gmk_power_protocol_reply_encode(out, malformed, &reply));
    for (unsigned offset = 0; offset < sizeof(out); ++offset)
        assert(out[offset] == 0xa5);
    memcpy(out, request, sizeof(out));
    assert(gmk_power_protocol_reply_encode(out, out, &reply));
    assert(!memcmp(out, response, sizeof(out)));
}
int main(void) {
    exact_wire_layout_and_checksum_match_known_packets();
    every_operation_phase_and_known_flag_combination_roundtrips();
    every_single_bit_corruption_is_rejected();
    invalid_semantics_fail_even_with_a_correct_checksum();
    replies_are_bound_to_exact_session_operation_critical_and_version();
    invalid_encoders_preserve_outputs_and_aliasing_remains_supported();
    puts("Power protocol: exact wire CRC, corruption, semantic bounds and stale-session binding passed");
    return 0;
}
