/* Copyright 2026 GMK70 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Wire protocol derived from QMK serial_protocol.c (Copyright 2022 Stefan
 * Kerkmann), split_common transactions/transport (Copyright 2021 QMK), and
 * crc.c (Copyright 2021 QMK). See installed-profile.md for exact provenance.
 */
#include "wired_protocol.h"
#include <string.h>

static const uint8_t input_sizes[19]  = {0, 0, 10, 0, 0, 0, 4, 1, 1, 1, 4, 16, 12, 4, 32, 1, 0, 0, 0};
static const uint8_t output_sizes[19] = {1, 10, 0, 1, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 0, 0};

uint8_t gmk_wire_crc8(const void *data, size_t size) {
    const uint8_t *bytes = data;
    uint8_t        crc   = 0xff;
    while (size--) {
        crc ^= *bytes++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (uint8_t)((crc << 1) ^ ((crc & 0x80) ? 0x31 : 0));
    }
    return crc;
}
void gmk_protocol_matrix(struct gmk_protocol *p, const uint16_t rows[5]) {
    for (unsigned r = 0; r < 5; ++r) {
        p->matrix[2 * r]     = (uint8_t)rows[r];
        p->matrix[2 * r + 1] = (uint8_t)(rows[r] >> 8);
    }
}
static void invalidate(struct gmk_protocol *p) {
    p->info_valid = p->request_valid = p->response_valid = false;
    p->request_size = p->response_size = 0;
    memset(p->request, 0, 32);
    memset(p->response, 0, 32);
}
bool gmk_protocol_serve(struct gmk_protocol *p, const struct gmk_wire_io *io) {
    uint8_t id, in[32] = {0}, out[32] = {0};
    if (!io->read(io->ctx, &id, 1) || id >= 19) goto fail;
    uint8_t ack = id ^ GMK_WIRE_TRANSACTIONS;
    if (!io->write(io->ctx, &ack, 1)) goto fail;
    size_t ni = input_sizes[id], no = output_sizes[id];
    if (id == 14) {
        if (!p->info_valid) goto fail;
        ni = p->request_size;
    }
    if (id == 16) {
        if (!p->response_valid) goto fail;
        no = p->response_size;
    }
    if (ni && !io->read(io->ctx, in, ni)) goto fail;
    switch (id) {
        case 0:
            out[0] = gmk_wire_crc8(p->matrix, 10);
            break;
        case 1:
            memcpy(out, p->matrix, 10);
            break;
        case 3:
            out[0] = gmk_wire_crc8(out, 8);
            break;
        case 13:
            invalidate(p);
            if (in[0] != gmk_wire_crc8(in + 1, 3) || in[1] < 17 || in[1] >= 19 || in[2] > 32 || in[3] > 32) goto fail;
            p->rpc_id        = in[1];
            p->request_size  = in[2];
            p->response_size = in[3];
            p->info_valid    = true;
            break;
        case 14:
            memcpy(p->request, in, ni);
            p->request_valid  = true;
            p->response_valid = false;
            memset(p->response, 0, 32);
            break;
        case 15:
            if (!p->info_valid || !p->request_valid || in[0] != p->rpc_id) goto fail;
            if (p->rpc_id == 18) {
                if (p->request_size != 32 || p->response_size != 32 || !p->update) goto fail;
                /* The registered callback only queues work / reads its cached
                 * response. Flash operations MUST execute outside this thread. */
                p->update(p->request, p->response);
            }
            p->request_valid  = false;
            p->response_valid = true;
            break;
        case 16:
            memcpy(out, p->response, no);
            break;
        default:
            break; /* Consume core synchronization without EEPROM/radio side effects. */
    }
    if (no && !io->write(io->ctx, out, no)) goto fail;
    if (id == 16) invalidate(p);
    return true;
fail:
    invalidate(p);
    io->clear(io->ctx);
    return false;
}
static bool transaction(const struct gmk_wire_io *io, uint8_t id, const uint8_t *in, size_t ni, uint8_t *out, size_t no) {
    uint8_t ack;
    io->clear(io->ctx);
    return io->write(io->ctx, &id, 1) && io->read(io->ctx, &ack, 1) && ack == (id ^ 19) && (!ni || io->write(io->ctx, in, ni)) && (!no || io->read(io->ctx, out, no));
}
bool gmk_protocol_update(const struct gmk_wire_io *io, const uint8_t in[32], uint8_t out[32]) {
    uint8_t info[4] = {0, 18, 32, 32}, id = 18, response[32] = {0};
    memset(out, 0, 32);
    info[0] = gmk_wire_crc8(info + 1, 3);
    if (!transaction(io, 13, info, 4, NULL, 0) || !transaction(io, 14, in, 32, NULL, 0) || !transaction(io, 15, &id, 1, NULL, 0) || !transaction(io, 16, NULL, 0, response, 32)) {
        io->clear(io->ctx);
        return false;
    }
    memcpy(out, response, 32);
    return true;
}
bool gmk_protocol_poll(const struct gmk_wire_io *io, uint16_t rows[5]) {
    uint8_t checksum, data[10];
    memset(rows, 0, 10);
    if (!transaction(io, 0, NULL, 0, &checksum, 1) || !transaction(io, 1, NULL, 0, data, 10) || checksum != gmk_wire_crc8(data, 10)) return false;
    for (unsigned r = 0; r < 5; ++r)
        rows[r] = (uint16_t)(data[2 * r] | ((uint16_t)data[2 * r + 1] << 8));
    return true;
}
void gmk_matrix_debounce(struct gmk_debounce *d, const uint16_t raw[5], uint32_t now) {
    for (unsigned r = 0; r < 5; ++r)
        for (unsigned c = 0; c < 9; ++c) {
            uint16_t mask = (uint16_t)(1U << c);
            if ((raw[r] ^ d->candidate[r]) & mask) {
                d->candidate[r] ^= mask;
                d->since[r][c] = now;
            }
            if ((uint32_t)(now - d->since[r][c]) >= 5) d->stable[r] = (d->stable[r] & ~mask) | (d->candidate[r] & mask);
        }
}
void gmk_peer_set(struct gmk_peer *p, const uint16_t rows[5], uint32_t now) {
    memcpy(p->rows, rows, 10);
    p->seen      = now;
    p->connected = true;
}
bool gmk_peer_rows(struct gmk_peer *p, uint32_t now, uint16_t rows[5]) {
    if (p->connected && (uint32_t)(now - p->seen) >= GMK_PEER_TIMEOUT_MS) p->connected = false;
    if (p->connected)
        memcpy(rows, p->rows, 10);
    else
        memset(rows, 0, 10);
    return p->connected;
}
