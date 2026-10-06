// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "image.h"

enum { GMK_PACKET_SIZE = 32, GMK_DATA_SIZE = 16, GMK_UPDATE_COMMAND = 0x7F };
typedef enum { GMK_STATUS = 1, GMK_BEGIN, GMK_DATA, GMK_SEAL, GMK_INSTALL, GMK_ABORT, GMK_INFO, GMK_EEPROM_INFO, GMK_EEPROM_READ, GMK_SLEEP_LOG_INFO, GMK_SLEEP_LOG_READ } gmk_update_op_t;
typedef enum { GMK_IDLE, GMK_RECEIVING, GMK_SEALED, GMK_COMMITTED, GMK_FAILED } gmk_update_state_t;
typedef enum { GMK_OK, GMK_BUSY, GMK_BAD_REQUEST, GMK_BAD_STATE, GMK_BAD_OFFSET, GMK_FLASH_ERROR, GMK_BAD_IMAGE, GMK_UNAVAILABLE } gmk_update_result_t;
typedef struct {
    gmk_update_state_t state;
    gmk_image_t        image;
    uint32_t           session;
    uint32_t           received;
    uint8_t            page[GMK_PAGE_SIZE];
} gmk_update_t;

bool                gmk_packet_valid(const uint8_t request[GMK_PACKET_SIZE]);
gmk_update_result_t gmk_update_process(gmk_update_t *update, const uint8_t request[GMK_PACKET_SIZE]);
void                gmk_update_response(const gmk_update_t *update, const uint8_t request[GMK_PACKET_SIZE], gmk_update_result_t result, uint8_t response[GMK_PACKET_SIZE]);
