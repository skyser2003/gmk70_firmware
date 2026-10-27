/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define GMK_WIRE_TRANSACTIONS 19
#define GMK_WIRE_TIMEOUT_MS 20
#define GMK_PEER_TIMEOUT_MS 100
struct gmk_wire_io {
    void *ctx;
    bool (*read)(void *, uint8_t *, size_t);
    bool (*write)(void *, const uint8_t *, size_t);
    void (*clear)(void *);
};
struct gmk_protocol {
    uint8_t matrix[10], request[32], response[32];
    uint8_t rpc_id, request_size, response_size;
    bool    info_valid, request_valid, response_valid;
    void (*update)(const uint8_t[32], uint8_t[32]);
};
struct gmk_debounce {
    uint16_t candidate[5], stable[5];
    uint32_t since[5][9];
};
struct gmk_peer {
    uint16_t rows[5];
    uint32_t seen;
    bool     connected;
};
uint8_t gmk_wire_crc8(const void *data, size_t size);
void    gmk_protocol_matrix(struct gmk_protocol *, const uint16_t rows[5]);
bool    gmk_protocol_serve(struct gmk_protocol *, const struct gmk_wire_io *);
bool    gmk_protocol_update(const struct gmk_wire_io *, const uint8_t[32], uint8_t[32]);
bool    gmk_protocol_poll(const struct gmk_wire_io *, uint16_t rows[5]);
void    gmk_matrix_debounce(struct gmk_debounce *, const uint16_t raw[5], uint32_t now);
void    gmk_peer_set(struct gmk_peer *, const uint16_t rows[5], uint32_t now);
bool    gmk_peer_rows(struct gmk_peer *, uint32_t now, uint16_t rows[5]);
