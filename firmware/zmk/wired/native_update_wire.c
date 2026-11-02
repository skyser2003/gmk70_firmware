/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native_update_wire.h"
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <wired/wired.h>

enum {
    packet_size           = 32,
    extension_type        = 0x80,
    request_payload_size  = 1 + 1 + packet_size,
    response_payload_size = 1 + 4 + packet_size,
    max_frame_size        = sizeof(struct msg_prefix) + response_payload_size + sizeof(struct msg_postfix),
};
static const uint8_t capability_request[32] = {'Z', 'M', 'K', 'U', 1};

void gmk_native_update_capability_request(uint8_t request[32]) {
    memcpy(request, capability_request, sizeof(capability_request));
}

bool gmk_native_update_capability(const uint8_t request[32], uint8_t response[32]) {
    if (memcmp(request, capability_request, sizeof(capability_request))) return false;
    memcpy(response, capability_request, sizeof(capability_request));
    response[5] = 1; /* Native updater carrier supported. */
    response[6] = response[7] = extension_type;
    sys_put_le32(crc32_ieee(response, 28), response + 28);
    return true;
}

static size_t payload_size(bool response) {
    return response ? response_payload_size : request_payload_size;
}

static bool write_frame(const struct gmk_wire_io *io, const uint8_t packet[32], bool response) {
    uint8_t           frame[max_frame_size] = {0};
    struct msg_prefix prefix                = {.payload_size = (uint8_t)payload_size(response)};
    memcpy(prefix.magic_prefix, ZMK_SPLIT_WIRED_ENVELOPE_MAGIC_PREFIX, sizeof(prefix.magic_prefix));
    memcpy(frame, &prefix, sizeof(prefix));
    /* Source/destination 0 is the sole peripheral. Native central command
     * enums occupy one byte; peripheral event enums occupy four on WB32. */
    frame[sizeof(prefix) + 1] = extension_type;
    size_t offset             = sizeof(prefix) + payload_size(response) - packet_size;
    memcpy(frame + offset, packet, packet_size);
    size_t end = offset + packet_size;
    sys_put_le32(crc32_ieee(frame, end), frame + end);
    return io->write(io->ctx, frame, end + sizeof(struct msg_postfix));
}

static bool read_frame(const struct gmk_wire_io *io, uint8_t packet[32], bool response) {
    uint8_t         storage[max_frame_size];
    uint8_t         envelope[max_frame_size - sizeof(struct msg_postfix)];
    struct ring_buf ring;
    ring_buf_init(&ring, sizeof(storage), storage);
    size_t   size = sizeof(struct msg_prefix) + payload_size(response) + sizeof(struct msg_postfix);
    uint8_t *data;
    if (ring_buf_put_claim(&ring, &data, size) != size || !io->read(io->ctx, data, size) || ring_buf_put_finish(&ring, size)) return false;
    /* Reuse the unchanged upstream parser, bounds checks and CRC32 verifier.
     * Fixed frame sizes also bound corrupt-length and partial-frame reads. */
    if (zmk_split_wired_get_item(&ring, envelope, sizeof(envelope)) || ring_buf_size_get(&ring) != 0) return false;
    size_t prefix = sizeof(struct msg_prefix);
    if (envelope[prefix - 1] != payload_size(response) || envelope[prefix] != 0 || envelope[prefix + 1] != extension_type) return false;
    if (response && (envelope[prefix + 2] || envelope[prefix + 3] || envelope[prefix + 4])) return false;
    memcpy(packet, envelope + prefix + payload_size(response) - packet_size, packet_size);
    return true;
}

bool gmk_native_update_exchange(const struct gmk_wire_io *io, const uint8_t request[32], uint8_t response[32]) {
    memset(response, 0, packet_size);
    io->clear(io->ctx);
    if (!write_frame(io, request, false) || !read_frame(io, response, true)) {
        memset(response, 0, packet_size);
        io->clear(io->ctx);
        return false;
    }
    return true;
}

bool gmk_native_update_serve(const struct gmk_wire_io *io, gmk_update_wire_handler_t handler) {
    uint8_t request[32], response[32] = {0};
    if (!handler || !read_frame(io, request, false)) return false;
    /* Existing handler only queues/reads a cached reply. Flash stays on the
     * existing service worker, with all original role/mode/storage gates. */
    handler(request, response);
    return write_frame(io, response, true);
}
